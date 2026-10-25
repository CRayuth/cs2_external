#include <stdafx.hpp>
#include <features/aimbot/command_steering.hpp>
#include <ui/menu/internal.hpp>
#include <ui/menu/combat_widgets.hpp>

namespace ui
{
using namespace ui::menu::detail;
using namespace ui::menu::combat;

void menu_t::draw_aimbot()
{
    begin_combat_page(false);
    auto& global = config::combat_settings.global;
    if (m_weapon_group < 0)
    {
        const auto aim_uses_key = global.aimbot_activation_mode != config::combat_profile::activation::always;
        const auto targeting_rows = global.aimbot_enabled ? (aim_uses_key ? 10 : 9) : 1;
        card_in_column("targeting", "TARGETING", targeting_rows, 0,
                       [&]
                       {
                           master_row(global.aimbot_enabled, global.aimbot_activation_mode);
                           if (global.aimbot_enabled)
                           {
                               if (aim_uses_key)
                                   keybind_row("Key", global.aimbot_key);
                               checks_row(global.aimbot_checks);
                               aim_parts_row(global.aimbot_hitbox_parts);
                               fov_row(global.aimbot_fov, global.aimbot_fov_config, global.aimbot_draw_fov,
                                       global.aimbot_fov_color);
                               humanizer_row(global.aimbot_humanize, global.aimbot_smoothing, global.aimbot_humanizer);
                               multipoint_row(global.aimbot_multipoint, global.aimbot_multipoint_config);
                               prediction_row(global.aimbot_prediction);
                               toggle_row("Lethal Only", global.aimbot_lethal_only);
                               toggle_popup_row("Silent Aim", global.aimbot_silent, 4,
                                                [&]
                                                {
                                                    slider_row("Max Silent Turn", global.aimbot_silent_max_turn, 5.0f,
                                                               35.0f, "°", 1.0f);
                                                    // 0 turns the shot onto the target with the next command; a higher
                                                    // value spreads the correction over several commands. It is
                                                    // separate from the mouse smoothing because the camera never moves.
                                                    slider_row("Silent Smoothness", global.aimbot_silent_smoothing, 0, 10);
                                                    slider_row("Circle Size", global.aimbot_fov_draw_scale, 5.0f,
                                                               400.0f, "%", 5.0f);
                                                    // The toggle says what was asked for; this line says whether the aim
                                                    // actually travels through the command ring or fell back to the mouse.
                                                    const auto& steering = features::aimbot::command_steering();
                                                    const auto active = steering.healthy();
                                                    const char* status =
                                                        active ? "Steers the shot"
                                                        : steering.available() ? "Input fallback: command ring unavailable"
                                                                               : "Input fallback: handle is read-only";
                                                    ImGui::TextColored(active ? k_text_muted
                                                                              : ImVec4{1.0f, 0.42f, 0.45f, 1.0f},
                                                                       "%s", render::localization::tr(status));
                                                });
                           }
                       });
        card_in_column("recoil", "RECOIL CONTROL", 1, 1, [&] { rcs_row(global.aimbot_rcs); });
        if (global.aimbot_enabled)
            card_in_column("penetration", "PENETRATION", 3, 1,
                           [&]
                           {
                               visibility_row(global.aimbot_checks);
                               slider_row("Min Damage", global.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f);
                               damage_override_row(
                                   global.aimbot_min_damage_override_enabled, global.aimbot_min_damage_override,
                                   global.aimbot_min_damage_override_mode, global.aimbot_min_damage_override_key);
                           });
    }
    else
    {
        auto& group = config::combat_settings.overrides[m_weapon_group];
        card("override", "OVERRIDE SETTINGS", 1, [&] { toggle_row("Inherit Global Settings", group.use_global); });
        if (!group.use_global && global.aimbot_enabled)
        {
            card_in_column("targeting", "TARGETING", 8, 0,
                           [&]
                           {
                               checks_row(group.aimbot_checks);
                               aim_parts_row(group.aimbot_hitbox_parts);
                               fov_row(group.aimbot_fov, group.aimbot_fov_config, global.aimbot_draw_fov,
                                       global.aimbot_fov_color);
                               humanizer_row(group.aimbot_humanize, group.aimbot_smoothing, group.aimbot_humanizer);
                               multipoint_row(group.aimbot_multipoint, group.aimbot_multipoint_config);
                               prediction_row(group.aimbot_prediction);
                               toggle_row("Lethal Only", group.aimbot_lethal_only);
                               toggle_row("Silent Aim", group.aimbot_silent);
                           });
            card_in_column("recoil", "RECOIL CONTROL", 1, 1, [&] { rcs_row(group.aimbot_rcs); });
            card("penetration", "PENETRATION", 2,
                 [&]
                 {
                     visibility_row(group.aimbot_checks);
                     slider_row("Min Damage", group.aimbot_min_damage, 1.0f, 100.0f, "", 1.0f);
                 });
        }
    }
    end_combat_page();
}
} // namespace ui
