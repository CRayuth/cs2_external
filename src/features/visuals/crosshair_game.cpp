#include <stdafx.hpp>
#include <features/visuals/crosshair_game.hpp>
#include <features/misc/convar_io.hpp>
#include <game/memory/bound_services.hpp>
#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <unordered_map>

namespace features::visuals
{

namespace
{
[[nodiscard]] std::string number(const float value)
{
    // Four significant digits are enough for every crosshair dimension and keep the console text short.
    return std::format("{:.4g}", static_cast<double>(value));
}

[[nodiscard]] std::string toggle(const bool value) { return value ? "1" : "0"; }
} // namespace

std::vector<std::pair<std::string, std::string>>
crosshair_commands(const config::visual_profile::crosshair& settings)
{
    std::vector<std::pair<std::string, std::string>> commands{};
    commands.reserve(16);

    // Style 4 is the classic static shape: the only preset whose size, thickness, gap and dot the console lets us
    // set independently, which is what makes a 1:1 export of the overlay crosshair possible.
    commands.emplace_back("cl_crosshairstyle", "4");
    commands.emplace_back("cl_crosshair_drawoutline", toggle(settings.outline && settings.outline_color.a > 0));
    commands.emplace_back("cl_crosshair_outlinethickness",
                          number(std::clamp(settings.outline_thickness, 0.5f, 3.0f)));
    commands.emplace_back("cl_crosshairsize", number(std::clamp(settings.length, 0.0f, 200.0f)));
    commands.emplace_back("cl_crosshairthickness", number(std::clamp(settings.thickness, 0.0f, 10.0f)));
    commands.emplace_back("cl_crosshairgap", number(std::clamp(settings.gap, -20.0f, 20.0f)));
    commands.emplace_back("cl_crosshairdot", toggle(settings.dot));
    commands.emplace_back("cl_crosshair_t", toggle(settings.t_style));
    commands.emplace_back("cl_crosshair_recoil", "0");

    // Colour 5 means "use the explicit channels", so red, green and blue travel with the block.
    commands.emplace_back("cl_crosshaircolor", "5");
    commands.emplace_back("cl_crosshaircolor_r", std::format("{}", settings.color.r));
    commands.emplace_back("cl_crosshaircolor_g", std::format("{}", settings.color.g));
    commands.emplace_back("cl_crosshaircolor_b", std::format("{}", settings.color.b));
    commands.emplace_back("cl_crosshairusealpha", "1");
    commands.emplace_back("cl_crosshairalpha", std::format("{}", settings.color.a));

    if (settings.sniper_mode)
        commands.emplace_back("cl_crosshair_sniper_width",
                              number(std::clamp(settings.sniper_width, 0.0f, 10.0f)));

    return commands;
}

std::string crosshair_command_text(const config::visual_profile::crosshair& settings)
{
    std::string text{};
    for (const auto& [name, value] : crosshair_commands(settings))
        text += std::format("{} {}\n", name, value);
    return text;
}

crosshair_apply_result apply_crosshair_to_game(const config::visual_profile::crosshair& settings)
{
    crosshair_apply_result result{};
    result.writable = game::bound_memory().process.writable();
    if (!result.writable)
        return result;

    auto entries = misc::enumerate_convars();
    std::unordered_map<std::string, const misc::convar_entry*> index{};
    index.reserve(entries.size() * 2);
    for (const auto& entry : entries)
        index.try_emplace(entry.name, &entry);

    for (const auto& [name, value] : crosshair_commands(settings))
    {
        const auto found = index.find(name);
        if (found == index.end() || !misc::write_convar(*found->second, value))
            ++result.missing;
        else
            ++result.applied;
    }

    return result;
}

} // namespace features::visuals
