#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <config/settings.hpp>
#include <foundation/identity.hpp>
#include <game/input/action_state.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/constants.hpp>
#include <game/state/services.hpp>
#include <config/misc.hpp>
#include <platform/windows/input.hpp>
#include <render/presentation.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace features::misc
{

namespace
{
// Sends one press or release on the device the action is actually bound to. The bound key is what the game listens
// for, so nothing here needs a key of its own.
[[nodiscard]] bool action_press(const game::input_action action, const bool pressed)
{
    auto& input = platform::windows::bound_input();
    const auto binding = game::input_bindings().resolve(action);
    const auto pointer = [&](const platform::windows::pointer_action down,
                             const platform::windows::pointer_action up)
    { return input.pointer(0, 0, pressed ? down : up); };

    switch (binding.device)
    {
        case game::input_device::mouse_primary:
            return pointer(platform::windows::pointer_action::primary_down,
                           platform::windows::pointer_action::primary_up);
        case game::input_device::mouse_secondary:
            return pointer(platform::windows::pointer_action::secondary_down,
                           platform::windows::pointer_action::secondary_up);
        case game::input_device::mouse_middle:
            return pointer(platform::windows::pointer_action::middle_down,
                           platform::windows::pointer_action::middle_up);
        case game::input_device::mouse_auxiliary1:
            return pointer(platform::windows::pointer_action::auxiliary1_down,
                           platform::windows::pointer_action::auxiliary1_up);
        case game::input_device::mouse_auxiliary2:
            return pointer(platform::windows::pointer_action::auxiliary2_down,
                           platform::windows::pointer_action::auxiliary2_up);
        case game::input_device::keyboard:
            return binding.virtual_key != 0 && input.key(binding.virtual_key, pressed);
        case game::input_device::none:
        default:
            return false;
    }
}

[[nodiscard]] std::chrono::milliseconds to_milliseconds(const float value)
{
    return std::chrono::milliseconds(static_cast<int>(std::lround(std::clamp(value, 1.0f, 5000.0f))));
}
} // namespace

bool automation_t::injection_allowed() const
{
    auto& input = platform::windows::bound_input();
    if (!input.connected() || !input.accepts_input())
        return false;
    // A read-only handle means the game is not the process Vesta attached to, so there is nothing to drive.
    if (!game::bound_memory().process)
        return false;
    return !render::menu_open() && !game::input_bindings().text_entry_active();
}

void automation_t::tick_auto_pistol()
{
    const auto settings = config::get_runtime_snapshot();
    const auto& cfg = settings->general.m_automation.auto_pistol;
    const auto now = std::chrono::steady_clock::now();

    const auto release = [this]
    {
        if (!m_trigger_down)
            return;
        static_cast<void>(action_press(game::input_action::attack, false));
        m_trigger_down = false;
    };

    if (!cfg.enabled || !injection_allowed())
    {
        release();
        return;
    }

    const auto local = game::local_player().snapshot();
    // Only sidearms are pumped: an automatic pistol already fires on its own, and holding a rifle trigger for the
    // player would fight his own aim.
    if (!local->alive || local->weapon_type != game::rules::sidearm || !local->weapon || !local->weapon_vdata)
    {
        release();
        return;
    }

    const auto& process = game::bound_memory().process;
    const auto automatic =
        process.load<bool>(local->weapon_vdata + SCHEMA("CCSWeaponBaseVData", "m_bIsFullAuto"_id));
    const auto clip = process.load<std::int32_t>(local->weapon + SCHEMA("C_BasePlayerWeapon", "m_iClip1"_id));
    const auto reloading = process.load<bool>(local->weapon + SCHEMA("C_CSWeaponBase", "m_bInReload"_id));
    const auto user_firing = game::physical_action_down(game::input_action::attack,
                                                        platform::windows::bound_input());
    if (automatic || clip <= 0 || reloading || user_firing)
    {
        release();
        return;
    }

    const auto hold = to_milliseconds(cfg.hold_ms);
    if (m_trigger_down)
    {
        if (now - m_trigger_stamp >= hold)
            release();
        return;
    }

    if (now < m_next_shot)
        return;

    if (action_press(game::input_action::attack, true))
    {
        m_trigger_down = true;
        m_trigger_stamp = now;
        // The interval is measured from the start of the press, so hold and gap add up to one cycle.
        m_next_shot = now + hold + to_milliseconds(cfg.interval_ms);
    }
}

void automation_t::tick_anti_afk()
{
    const auto settings = config::get_runtime_snapshot();
    const auto& cfg = settings->general.m_automation.anti_afk;
    const auto now = std::chrono::steady_clock::now();

    const auto release = [this]
    {
        if (!m_afk_down)
            return;
        static_cast<void>(action_press(game::input_action::forward, false));
        m_afk_down = false;
    };

    if (!cfg.enabled || !injection_allowed())
    {
        m_afk_armed = false;
        release();
        return;
    }

    auto& input = platform::windows::bound_input();
    // Any movement the player produces himself restarts the clock, so the tap only ever happens on a real idle.
    const auto moving = game::physical_action_down(game::input_action::forward, input) ||
                        game::physical_action_down(game::input_action::back, input) ||
                        game::physical_action_down(game::input_action::left, input) ||
                        game::physical_action_down(game::input_action::right, input) ||
                        game::physical_action_down(game::input_action::jump, input) ||
                        game::physical_action_down(game::input_action::duck, input);
    if (moving || !game::local_player().alive())
    {
        m_afk_armed = false;
        release();
        return;
    }

    const auto idle = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<float>(std::clamp(cfg.idle_seconds, 5.0f, 300.0f)));
    const auto hold = to_milliseconds(cfg.hold_ms);

    if (!m_afk_armed)
    {
        m_afk_armed = true;
        m_afk_due = now + idle;
        return;
    }

    if (m_afk_down)
    {
        if (now - m_afk_stamp >= hold)
            release();
        return;
    }

    if (now < m_afk_due)
        return;

    if (action_press(game::input_action::forward, true))
    {
        m_afk_down = true;
        m_afk_stamp = now;
        // The next tap waits a full idle window after this one ends, so a tap that arrives late never shortens it.
        m_afk_due = now + hold + idle;
    }
}

void automation_t::tick()
{
    tick_auto_pistol();
    tick_anti_afk();
}

} // namespace features::misc
