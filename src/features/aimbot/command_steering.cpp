#include <stdafx.hpp>
#include <features/aimbot/command_steering.hpp>
#include <diagnostics/journal.hpp>
#include <game/memory/bound_services.hpp>

namespace features::aimbot
{
namespace
{
[[nodiscard]] std::uint64_t pack_angles(const float pitch, const float yaw) noexcept
{
    return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(pitch)) << 32) |
           static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(yaw));
}

void unpack_angles(const std::uint64_t packed, float& pitch, float& yaw) noexcept
{
    pitch = std::bit_cast<float>(static_cast<std::uint32_t>(packed >> 32));
    yaw = std::bit_cast<float>(static_cast<std::uint32_t>(packed & 0xffffffffu));
}
} // namespace

std::int64_t command_steering_t::now_ns() const noexcept
{
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now().time_since_epoch())
                                         .count());
}

void command_steering_t::publish(const foundation::vec3& angles) noexcept
{
    // Validated here rather than trusted from the caller: the published value is written straight into a
    // command the server will shoot with, so a NaN or an unwrapped yaw must never reach the ring.
    if (!std::isfinite(angles.x) || !std::isfinite(angles.y))
        return;

    // A near-vertical angle is clamped, not dropped. The caller has already committed to this transport and
    // suppressed the mouse move, so refusing the publish would let the shot leave along the player's own view
    // while the aim pass believed it steered. The engine never aims past this pitch in the first place.
    const auto pitch = std::clamp(angles.x, -89.0f, 89.0f);
    const auto yaw = foundation::wrap_yaw(angles.y);

    const auto stamp = now_ns();
    m_angles.store(pack_angles(pitch, yaw), std::memory_order_relaxed);
    // Released after the angle so a reader that sees the new stamp also sees the angle it belongs to.
    m_published_at.store(stamp, std::memory_order_release);
}

void command_steering_t::clear() noexcept
{
    std::scoped_lock lock(m_state_mutex);
    m_published_at.store(0, std::memory_order_release);
    m_angles.store(0, std::memory_order_relaxed);
    m_last_stamp = 0;
    m_reported_unavailable = false;
    // The failure count is deliberately not reset here: clear() runs on every pass that aims with the mouse,
    // and that would erase the very evidence that made the aim pass fall back.
}

void command_steering_t::rearm() noexcept
{
    std::scoped_lock lock(m_state_mutex);
    m_write_failures.store(0, std::memory_order_release);
    m_reported_unavailable = false;
}

void command_steering_t::report(const std::chrono::steady_clock::time_point now, const bool writable,
                                const bool written) noexcept
{
    // The overlay is deliberately invisible to screen capture whenever OBS bypass is on, so the journal is the
    // only surviving record that a steered angle actually reached the game. One note a second, and only while
    // the counters move: a note that repeats the same numbers proves nothing.
    const auto steered = m_steered.load(std::memory_order_relaxed);
    const auto failed = m_failed.load(std::memory_order_relaxed);
    if (steered == m_reported_steered && failed == m_reported_failed)
        return;
    if (!m_note.allow(now))
        return;

    const auto landed = static_cast<std::uint32_t>(steered - m_reported_steered);
    const auto refused = static_cast<std::uint32_t>(failed - m_reported_failed);
    m_reported_steered = steered;
    m_reported_failed = failed;

    // How diagnostics::name() labels this record, "silentaim.state", reads its code_domain::mode word:
    //   bits  0..15 writes that landed since the last note, saturating at 65535
    //   bits 16..23 writes that failed since the last note, saturating at 255
    //   bit  24       a request was held for this pass (always set: no request, no note)
    //   bit  25       the process handle was writable
    //   bit  26       this pass wrote the command
    //   bit  27       the service is unhealthy, so the aim pass aims with the mouse
    auto code = static_cast<std::uint32_t>(std::min<std::uint32_t>(landed, 0xffffu));
    code |= std::min<std::uint32_t>(refused, 0xffu) << 16;
    code |= 1u << 24;
    code |= writable ? 1u << 25 : 0u;
    code |= written ? 1u << 26 : 0u;
    code |= healthy() ? 0u : 1u << 27;
    diagnostics::emit(diagnostics::event::silent_aim_state, diagnostics::severity::information,
                      diagnostics::code_domain::mode, code, diagnostics::subject::combat);
}

bool command_steering_t::take_stamped(std::uint64_t& out_stamp, foundation::vec3& out_angles) const noexcept
{
    const auto stamp = m_published_at.load(std::memory_order_acquire);
    if (stamp == 0)
        return false;
    const auto now = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
    // Expire on age instead of waiting for the aim pass to clear the request: the pass runs on its own
    // cadence, and a request left hanging would steer commands long after the target was gone.
    if (now - static_cast<std::uint64_t>(stamp) >
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(k_request_ttl).count()))
        return false;

    float pitch{};
    float yaw{};
    const auto packed = m_angles.load(std::memory_order_relaxed);
    // A clear() from another thread between the stamp load and the angle load would pair a still-live stamp
    // with the zeroed angles, and 0,0 passes every range test below as a perfectly legal command angle.
    // Re-check the stamp: it only ever moves forward, so a changed value means this request is gone.
    if (m_published_at.load(std::memory_order_acquire) != stamp)
        return false;
    unpack_angles(packed, pitch, yaw);
    if (!std::isfinite(pitch) || !std::isfinite(yaw) || std::abs(pitch) > 89.9f || std::abs(yaw) > 180.0f)
        return false;

    out_stamp = stamp;
    out_angles = {pitch, yaw, 0.0f};
    return true;
}

std::optional<foundation::vec3> command_steering_t::peek() const noexcept
{
    std::uint64_t stamp{};
    foundation::vec3 angles{};
    if (!take_stamped(stamp, angles))
        return std::nullopt;
    return angles;
}

bool command_steering_t::armed() const noexcept
{
    std::uint64_t stamp{};
    foundation::vec3 angles{};
    return take_stamped(stamp, angles);
}

bool command_steering_t::available() const noexcept
{
    return game::bound_memory().process.writable();
}

void command_steering_t::tick()
{
    std::scoped_lock lock(m_state_mutex);
    std::uint64_t stamp{};
    foundation::vec3 angles{};
    if (!take_stamped(stamp, angles))
        return;

    const auto& memory = game::bound_memory();
    const auto now = std::chrono::steady_clock::now();
    const auto writable = memory.process.writable();
    auto written = false;
    if (!writable)
    {
        if (!m_reported_readonly)
        {
            m_reported_readonly = true;
            memory.log.warning("[silent-aim] disabled: process handle is read-only");
        }
    }
    // An already applied request is not written again, and two writes per command period is the cap: the
    // command currently being built gets the freshest angle, history is left alone.
    else if (stamp != m_last_stamp && now - m_last_write_at >= k_min_write_interval)
    {
        int sequence{};
        if (m_commands.write_angles(angles, &sequence))
        {
            m_write_failures.store(0, std::memory_order_release);
            m_reported_unavailable = false;
            m_last_stamp = stamp;
            m_last_write_at = now;
            m_steered.fetch_add(1, std::memory_order_relaxed);
            written = true;
        }
        else
        {
            m_failed.fetch_add(1, std::memory_order_relaxed);
            const auto failures = m_write_failures.fetch_add(1, std::memory_order_release) + 1;
            if (failures == 32 && !m_reported_unavailable)
            {
                m_reported_unavailable = true;
                memory.log.warning("[silent-aim] the command ring cannot be resolved; aiming stays on input");
            }
        }
    }
    report(now, writable, written);
}

} // namespace features::aimbot
