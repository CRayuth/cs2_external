#include <stdafx.hpp>
#include <features/misc/misc.hpp>
#include <features/visuals/hitsound.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace features::misc
{

namespace
{
// True while the game window owns the foreground. The alert is meant to reach a player who looked away from the game,
// so when this is false the sound is the only thing that can tell him the round started.
[[nodiscard]] bool game_has_input_focus()
{
    const auto foreground = ::GetForegroundWindow();
    const auto root = foreground ? ::GetAncestor(foreground, GA_ROOT) : nullptr;
    DWORD process_id{};
    if (root)
        ::GetWindowThreadProcessId(root, &process_id);
    return process_id != 0 && process_id == game::bound_memory().process.process_id();
}
} // namespace

void round_alert_t::tick()
{
    const auto local = game::local_player().snapshot();
    const auto& cfg = config::get_runtime_snapshot()->general.m_round_alert;

    // Joining a match while already alive is not a spawn, so the first sample only records the state.
    if (!m_primed)
    {
        m_primed = local->pawn != 0;
        m_was_alive = local->alive;
        return;
    }

    const auto spawned = local->alive && !m_was_alive;
    m_was_alive = local->alive;
    if (!spawned || !cfg.enabled)
        return;
    if (cfg.when_unfocused && game_has_input_focus())
        return;

    features::visuals::hitsounds().play(std::clamp(cfg.style, 0, 4), std::clamp(cfg.volume, 0.0f, 1.0f));
}

} // namespace features::misc
