#include <stdafx.hpp>
#include <foundation/identity.hpp>
#include <config/misc.hpp>
#include <config/settings.hpp>
#include <game/memory/bound_services.hpp>
#include <game/state/services.hpp>
#include <game/state/pose.hpp>
#include <platform/windows/input.hpp>
#include <render/presentation.hpp>
#include <game/input/bindings.hpp>
#include <diagnostics/journal.hpp>
#include <features/misc/misc.hpp>

namespace features::misc
{
namespace
{
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

void speed_hack_t::tick()
{
    const auto snapshot = config::get_runtime_snapshot();
    const auto clear_state = [this]
    {
        // Forget any baseline we remembered for the previous pawn or session: the engine's next write
        // becomes the fresh reference the moment the feature reactivates.
        m_baseline = 0.0f;
        m_last_written = 0.0f;
        m_pawn = 0;
    };
    if (!snapshot || !snapshot->general.m_speed_hack.enabled)
    {
        clear_state();
        return;
    }
    const auto& cfg = snapshot->general.m_speed_hack;
    // active() folds the enabled check and the tri-state (always / hold / toggle) into one answer.
    // In always-on mode the key slot is ignored; in toggle mode a press flips the latch; in hold
    // mode the effect follows the physical key exactly like Bunny Hop and Edge Jump do.
    if (!cfg.active())
    {
        clear_state();
        return;
    }

    // Only drive speed while the game owns the foreground, the local player is alive, and the menu is
    // closed. Anything else would either modify an entity the player is not controlling or run against
    // the pause / spectate state, and the user would see a stale speed change bleed into the next round.
    if (!game_has_input_focus())
    {
        clear_state();
        return;
    }
    if (render::menu_open() || game::input_bindings().text_entry_active())
    {
        clear_state();
        return;
    }
    const auto pawn = game::local_player().pawn();
    if (!pawn || !game::local_player().alive())
    {
        clear_state();
        return;
    }
    // The process handle must be writable for this feature to do anything. Silent aim logs a warning
    // on the first failure; speed hack does the same so the user can tell a disabled feature from one
    // that is silently blocked by the anti-cheat attached with read-only access.
    if (!game::bound_memory().process.writable())
    {
        if (!m_reported_readonly)
        {
            m_reported_readonly = true;
            game::bound_memory().log.warning("[speed-hack] disabled: process handle is read-only.");
        }
        clear_state();
        return;
    }

    // A pawn change (death, respawn, map load) invalidates the remembered baseline.
    if (pawn != m_pawn)
    {
        m_baseline = 0.0f;
        m_last_written = 0.0f;
        m_pawn = pawn;
    }

    const auto address = pawn + SCHEMA("C_BasePlayerPawn", "m_flMaxspeed"_id);
    if (address == pawn)
    {
        // Field lookup failed on this build: leaving the value alone beats writing to offset 0.
        // One warning per session so a silent no-op does not look like a working feature from the user's seat.
        if (!m_reported_missing_field)
        {
            m_reported_missing_field = true;
            game::bound_memory().log.warning(
                "[speed-hack] C_BasePlayerPawn::m_flMaxspeed is not in the live schema; feature stays off.");
        }
        return;
    }
    if (!m_reported_offset)
    {
        m_reported_offset = true;
        game::bound_memory().log.info("[speed-hack] resolved m_flMaxspeed at pawn+{:#x}, writable={}.",
                                      static_cast<std::uint32_t>(address - pawn),
                                      game::bound_memory().process.writable() ? 1 : 0);
    }

    const auto current = game::bound_memory().process.load<float>(address);
    if (!std::isfinite(current) || current <= 0.0f)
    {
        m_baseline = 0.0f;
        m_last_written = 0.0f;
        return;
    }

    // If the field still holds what we last wrote, the engine has not recomputed speed since: keep the
    // same baseline and re-emit the same value only when it drifts. If it differs, the engine replaced
    // the field with a fresh value (walk / duck / weapon swap / air state): capture that as the new
    // baseline so the multiplier is never compounded on top of our own previous write.
    if (m_last_written == 0.0f || current != m_last_written)
        m_baseline = current;

    const auto target = m_baseline * cfg.multiplier;
    if (!std::isfinite(target) || target <= 0.0f)
        return;

    if (target == current)
    {
        // Value already carries the boost this tick; nothing to send. Clearing m_last_written signals
        // that the next differing read is the engine's own value, not a leftover from an earlier write.
        m_last_written = 0.0f;
        return;
    }

    if (game::bound_memory().process.store<float>(address, target))
    {
        m_last_written = target;
        // One confirmation log so the user can see the first successful write value.
        if (!m_reported_first_write)
        {
            m_reported_first_write = true;
            game::bound_memory().log.info("[speed-hack] first write: baseline={}, target={}, multiplier={}x.",
                                          m_baseline, target, cfg.multiplier);
        }
    }
    else if (!m_reported_store_failed)
    {
        m_reported_store_failed = true;
        game::bound_memory().log.warning("[speed-hack] WriteProcessMemory refused the value; feature stays off.");
    }
}

} // namespace features::misc
