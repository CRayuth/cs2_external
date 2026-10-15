#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <config/settings.hpp>
#include <game/state/services.hpp>
#include <game/memory/bound_services.hpp>
#include <features/misc/anti_flash.hpp>

namespace features::misc
{
namespace
{
constexpr auto k_pin_interval = std::chrono::milliseconds(15);

[[nodiscard]] std::int32_t flash_duration_offset()
{
    static const auto offset = []()
    {
        auto value = SCHEMA("C_CSPlayerPawnBase", "m_flFlashDuration"_id);
        if (value <= 0)
            value = SCHEMA("C_CSPlayerPawn", "m_flFlashDuration"_id);
        return value;
    }();
    return offset;
}

[[nodiscard]] std::int32_t flash_alpha_offset()
{
    static const auto offset = SCHEMA("C_CSPlayerPawnBase", "m_flFlashOverlayAlpha"_id);
    return offset;
}

[[nodiscard]] std::int32_t flash_max_alpha_offset()
{
    static const auto offset = SCHEMA("C_CSPlayerPawnBase", "m_flFlashMaxAlpha"_id);
    return offset;
}

[[nodiscard]] std::int32_t flash_bang_time_offset()
{
    static const auto offset = SCHEMA("C_CSPlayerPawnBase", "m_flFlashBangTime"_id);
    return offset;
}
} // namespace

void anti_flash_t::tick()
{
    const auto runtime = config::get_runtime_snapshot();
    if (!runtime)
    {
        this->m_armed.store(false, std::memory_order_relaxed);
        return;
    }

    const auto& settings = runtime->visual.m_no_flash;
    if (!settings.anti_flash)
    {
        this->m_armed.store(false, std::memory_order_relaxed);
        return;
    }

    if (!game::bound_memory().process.writable())
    {
        if (!this->m_reported)
        {
            this->m_reported = true;
            game::bound_memory().log.warning("[anti-flash] disabled: process handle is read-only");
        }
        this->m_armed.store(false, std::memory_order_relaxed);
        return;
    }

    const auto& local = game::local_player();
    const auto pawn = local.alive() ? local.pawn() : std::uintptr_t{};
    const auto duration_offset = flash_duration_offset();
    if (!pawn || duration_offset <= 0)
    {
        this->m_armed.store(false, std::memory_order_relaxed);
        return;
    }
    this->m_armed.store(true, std::memory_order_relaxed);

    const auto now = std::chrono::steady_clock::now();
    if (this->m_next_write > now)
        return;
    this->m_next_write = now + k_pin_interval;

    const auto& process = game::bound_memory().process;

    const auto duration_address = pawn + static_cast<std::uintptr_t>(duration_offset);
    const auto duration = process.load<float>(duration_address, -1.0f);
    if (detail::plausible_flash_seconds(duration) && process.store(duration_address, 0.0f))
    {
        this->m_writes.fetch_add(1, std::memory_order_relaxed);
    }

    const auto pin_positive = [&](const std::int32_t offset)
    {
        if (offset <= 0)
            return;
        const auto address = pawn + static_cast<std::uintptr_t>(offset);
        const auto value = process.load<float>(address, -1.0f);
        if (detail::flash_state_present(value) && process.store(address, 0.0f))
        {
            this->m_writes.fetch_add(1, std::memory_order_relaxed);
        }
    };

    pin_positive(flash_alpha_offset());
    pin_positive(flash_max_alpha_offset());
    pin_positive(flash_bang_time_offset());
}

} // namespace features::misc
