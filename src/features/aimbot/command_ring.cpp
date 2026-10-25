#include <stdafx.hpp>
#include <game/memory/remote_pointer.hpp>
#include <features/aimbot/command_ring.hpp>
#include <game/memory/bound_services.hpp>

namespace features::aimbot
{
namespace
{
constexpr std::uintptr_t k_slot_count{150};
constexpr std::uintptr_t k_slot_size{152};
// The counter sits immediately after the ring it belongs to, so this offset is structural rather than guessed.
constexpr std::uintptr_t k_counter_offset{k_slot_count * k_slot_size};
static_assert(k_counter_offset == 0x5910);

// CUserCmd -> the protobuf message it builds -> the view-angle field -> the three float values inside it.
constexpr std::uintptr_t k_command_message{0x40};
constexpr std::uintptr_t k_message_viewangles{0x40};
constexpr std::uintptr_t k_angles_values{0x18};
constexpr std::uintptr_t k_sequence_field{0x8};

[[nodiscard]] std::uintptr_t ring_array_address()
{
    static const auto value = []
    {
        const auto& memory = game::bound_memory();
        const auto instruction = memory.process.scan_signature(memory.modules.client, "4C 8B 35 ?? ?? ?? ?? 4C 63 F8");
        return instruction ? memory.process.decode_rip<std::uintptr_t>(instruction) : std::uintptr_t{};
    }();
    return value;
}

// The newest command of a ring whose counter and slot agree; the slot may lag the counter by one.
[[nodiscard]] bool validate_ring(const std::uintptr_t base, int& out_sequence)
{
    if (!game::is_user_pointer(base))
        return false;
    const auto& process = game::bound_memory().process;
    const auto counter = process.load<int>(base + k_counter_offset);
    for (int back = 0; back <= 1; ++back)
    {
        const auto candidate = counter - back;
        if (candidate <= 0)
            return false;
        const auto command = base + k_slot_size * (static_cast<std::uintptr_t>(candidate) % k_slot_count);
        if (process.load<int>(command + k_sequence_field) == candidate)
        {
            out_sequence = candidate;
            return true;
        }
    }
    return false;
}
} // namespace

bool command_ring_reader::fail(const std::chrono::steady_clock::time_point now)
{
    ++m_consecutive_failures;
    m_next_attempt = now + std::chrono::milliseconds(500);
    return false;
}

bool command_ring_reader::probe(const std::uintptr_t array, const int index,
                                const std::chrono::steady_clock::time_point now, view& out)
{
    const auto base = game::bound_memory().process.load<std::uintptr_t>(array + static_cast<std::uintptr_t>(index) *
                                                                                    sizeof(std::uintptr_t));
    int sequence{};
    if (!validate_ring(base, sequence))
        return false;
    if (m_seen_counter[index] != sequence)
    {
        m_seen_counter[index] = sequence;
        m_changed_at[index] = now;
        if (m_observed_changes[index] < 2)
            ++m_observed_changes[index];
    }
    out = {base, sequence};
    return true;
}

bool command_ring_reader::try_index(const std::uintptr_t array, const int index,
                                    const std::chrono::steady_clock::time_point now, view& out)
{
    view candidate{};
    if (!probe(array, index, now, candidate) || m_observed_changes[index] < 2)
        return false;
    // A live ring ticks 64 times a second; going quiet for a quarter second means it stopped being ours.
    if (now - m_changed_at[index] > std::chrono::milliseconds(250))
        return false;
    out = candidate;
    return true;
}

bool command_ring_reader::read(view& out)
{
    const auto array_address = ring_array_address();
    if (!array_address)
        return false;

    const auto now = std::chrono::steady_clock::now();
    if (m_consecutive_failures > 32 && now < m_next_attempt)
        return false;

    const auto array = game::bound_memory().process.load<std::uintptr_t>(array_address);
    if (!array)
        return fail(now);

    if (try_index(array, m_cached_index, now, out))
    {
        m_consecutive_failures = 0;
        return true;
    }

    // The cached ring went quiet: sample every candidate and follow the one that changed most recently.
    int freshest = -1;
    for (int index = 0; index < k_candidates; ++index)
    {
        view ignored{};
        static_cast<void>(probe(array, index, now, ignored));
        if (m_observed_changes[index] >= 2 && (freshest < 0 || m_changed_at[index] > m_changed_at[freshest]))
            freshest = index;
    }
    if (freshest >= 0 && try_index(array, freshest, now, out))
    {
        m_cached_index = freshest;
        m_consecutive_failures = 0;
        return true;
    }
    return fail(now);
}

bool command_ring_reader::resolve_angles(std::uintptr_t& out_address, int& out_sequence)
{
    view ring{};
    if (!read(ring))
        return false;
    const auto& process = game::bound_memory().process;
    const auto command = ring.base + k_slot_size * (static_cast<std::uintptr_t>(ring.sequence) % k_slot_count);
    const auto message = process.load<std::uintptr_t>(command + k_command_message);
    if (!game::is_user_pointer(message))
        return false;
    const auto angles = process.load<std::uintptr_t>(message + k_message_viewangles);
    if (!game::is_user_pointer(angles))
        return false;
    out_address = angles + k_angles_values;
    out_sequence = ring.sequence;
    return true;
}

bool command_ring_reader::read_angles(foundation::vec3& out, int* out_sequence)
{
    std::uintptr_t address{};
    int sequence{};
    if (!resolve_angles(address, sequence))
        return false;

    const auto& process = game::bound_memory().process;
    const auto pitch = process.load<float>(address);
    const auto yaw = process.load<float>(address + sizeof(float));
    if (!std::isfinite(pitch) || !std::isfinite(yaw) || std::abs(pitch) > 90.0f || std::abs(yaw) > 360.0f)
        return false;

    out = {pitch, yaw, 0.0f};
    if (out_sequence)
        *out_sequence = sequence;
    return true;
}

bool command_ring_reader::write_angles(const foundation::vec3& angles, int* out_sequence)
{
    // Refuse anything the client would not itself have produced: a bad ring resolution shows up here
    // before a single byte reaches the game rather than as a command that turns the shot arbitrarily.
    if (!std::isfinite(angles.x) || !std::isfinite(angles.y) || std::abs(angles.x) > 89.9f ||
        std::abs(angles.y) > 180.0f)
        return false;

    std::uintptr_t address{};
    int sequence{};
    if (!resolve_angles(address, sequence))
        return false;

    const auto& process = game::bound_memory().process;
    const float pair[2]{angles.x, angles.y};
    if (!process.copy_to(address, pair, sizeof(pair)))
        return false;

    // Read back: an already-consumed command or a stale ring shows up as a mismatch, so the caller
    // counts a failure instead of believing the shot was steered.
    if (process.load<float>(address, std::numeric_limits<float>::quiet_NaN()) != angles.x ||
        process.load<float>(address + sizeof(float), std::numeric_limits<float>::quiet_NaN()) != angles.y)
        return false;

    if (out_sequence)
        *out_sequence = sequence;
    return true;
}
} // namespace features::aimbot
