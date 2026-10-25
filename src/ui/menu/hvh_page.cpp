#include <stdafx.hpp>
#include <ui/menu/internal.hpp>
#include <ui/menu/menu.hpp>
#include <render/text/localization.hpp>
#include <features/hvh/hvh.hpp>

#include <imgui.h>

#include <chrono>
#include <cstring>
#include <string>

namespace ui
{
using namespace ui::menu::detail;

namespace
{
// The block stores its flags as int32 so both binaries agree on the layout, while the widgets work on bools.
void toggle_int(const char* label, std::int32_t& value)
{
    auto on = value != 0;
    toggle_row(label, on);
    value = on ? 1 : 0;
}

void status_row(const char* label, const char* value, const ImVec4 color)
{
    begin_row(label, 148.0f);
    ImGui::TextColored(color, "%s", value);
    end_row();
}
} // namespace

void menu_t::draw_hvh()
{
    auto& module = features::hvh::controller();
    auto& s = module.settings;
    // Opening the panel is what asks for the module: it reads hvh.json and maps the block, so the switches
    // show what is stored rather than the compiled defaults, and the worker starts polling a block that
    // exists. Idempotent, so drawing the page every frame costs one atomic read.
    module.initialize();
    // Snapshot of the config half before the widgets get at it. The struct is plain int32s, so a byte compare
    // is the whole question of whether this frame changed anything.
    const auto before = s;

    const char* state_text = render::localization::tr("Not Injected");
    ImVec4 state_color = k_text_muted;
    switch (module.status())
    {
    case features::hvh::inject_status::injected:
        state_text = render::localization::tr("Injected");
        state_color = ImVec4{0.38f, 0.86f, 0.55f, 1.0f};
        break;
    case features::hvh::inject_status::failed:
        state_text = render::localization::tr("Failed");
        state_color = ImVec4{1.0f, 0.38f, 0.42f, 1.0f};
        break;
    default:
        break;
    }

    ImGui::SetCursorPos({14.0f, 24.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
    ImGui::BeginChild("##hvh_cards", {682.0f, 612.0f}, false);
    begin_cards("##hvh_grid");

    if (m_hvh_inject_pending)
    {
        card_in_column("hvh_confirm", "CONFIRM INJECTION", 3, 0,
                       [&]
                       {
                           begin_row(render::localization::tr("Confirm"), 148.0f);
                           clipped_row_text(render::localization::tr("Inject vesta_hvh.dll into Counter-Strike 2?"));
                           end_row();
                           if (button_row(render::localization::tr("Proceed"), render::localization::tr("Inject"),
                                          row_action_icon::check))
                           {
                               module.inject();
                               m_hvh_inject_pending = false;
                           }
                           if (button_row(render::localization::tr("Abort"), render::localization::tr("Cancel")))
                           {
                               m_hvh_inject_pending = false;
                           }
                       });
    }

    card_in_column("hvh_status", "MODULE STATUS", 5, 0,
                   [&]
                   {
                       status_row(render::localization::tr("State"), state_text, state_color);
                       status_row(render::localization::tr("Hook"),
                                  module.hook_ready() ? render::localization::tr("Ready")
                                                      : render::localization::tr("Inactive"),
                                  module.hook_ready() ? k_text_main : k_text_muted);
                       status_row(render::localization::tr("Signature"),
                                  module.signature_found() ? render::localization::tr("Found")
                                                           : render::localization::tr("Missing"),
                                  module.signature_found() ? k_text_main : k_text_muted);
                       const auto calls = std::to_string(module.hook_calls());
                       status_row(render::localization::tr("Commands Steered"), calls.c_str(), k_text_muted);
                       const auto error = module.last_error();
                       status_row(render::localization::tr("Last Error"),
                                  error.empty() ? render::localization::tr("None") : std::string(error).c_str(),
                                  k_text_muted);
                   });

    card_in_column("hvh_injection", "INJECTION", 2, 0,
                   [&]
                   {
                       if (button_row(render::localization::tr("Configuration"), render::localization::tr("Inject"),
                                      row_action_icon::check) &&
                           !module.dll_active())
                       {
                           m_hvh_inject_pending = true;
                       }
                       if (button_row(render::localization::tr("Ejection"), render::localization::tr("Eject")))
                       {
                           module.eject();
                       }
                   });

    // Only the switches the module and the rage lock act on are offered here. The block carries more fields
    // than that - the reference drew them as working controls for behaviour nothing implements - and showing a
    // control that changes nothing is worse than not showing it. What is left says what it does.
    card_in_column("hvh_rage", "RAGE LOCK", 6, 1,
                   [&]
                   {
                       toggle_int("Enable Rage", s.enable_rage);
                       slider_row("FOV", s.rage_fov, 1, 180, " deg");
                       slider_row("Min Damage", s.rage_min_damage, 1, 100);
                       static constexpr const char* hitboxes[]{"Head", "Upper Chest", "Chest", "Pelvis", "Nearest"};
                       select_row("Hitbox", s.rage_hitbox, hitboxes);
                       toggle_int("Prefer Penetration", s.rage_autowall);
                       toggle_int("Silent Aim", s.rage_silent);
                   });

    card_in_column("hvh_silent", "SILENT TRANSPORT", 2, 0,
                   [&]
                   {
                       toggle_int("Mirror To Module", s.enable_silent);
                       begin_row(render::localization::tr("How it works"), 148.0f);
                       clipped_row_text(render::localization::tr(
                           "The aimbot picks one silent angle. The command ring always carries it; the module "
                           "stamps it into CreateMove as well while it is loaded."));
                       end_row();
                   });

    card_in_column("hvh_antiaim", "ANTI-AIM", 3, 0,
                   [&]
                   {
                       toggle_int("Enable Anti-Aim", s.enable_antiaim);
                       static constexpr const char* pitches[]{"Off", "Down", "Up", "Zero", "Fake Down"};
                       select_row("Pitch", s.aa_pitch, pitches);
                       slider_row("Spin Speed", s.aa_spin_speed, 1, 180, " deg");
                   });

    card_in_column("hvh_camera", "CAMERA", 1, 1,
                   [&]
                   {
                       toggle_int("Third Person", s.enable_thirdperson);
                   });

    end_cards();
    ImGui::EndChild();
    ImGui::PopStyleVar();

    if (std::memcmp(&before, &s, sizeof(s)) == 0)
    {
        return;
    }
    // The block is republished straight away: a switch flipped in the menu should reach a loaded module on the
    // next poll, not the next time the panel is opened. The file is a different matter - a dragged slider
    // changes this struct every frame, and every one of those changes would otherwise write it through a
    // temporary and rename it over the last. shutdown() writes the file for whatever the debounce leaves.
    module.publish();
    const auto now = std::chrono::steady_clock::now();
    if (m_hvh_saved_at.time_since_epoch().count() == 0 || now - m_hvh_saved_at >= std::chrono::seconds(2))
    {
        module.save();
        m_hvh_saved_at = now;
    }
}
} // namespace ui
