#pragma once

#include <config/visuals.hpp>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace features::visuals
{

// The overlay crosshair expressed as the game's own cl_crosshair_* settings, in the order the console wants them.
// CS2 styles are fixed presets, so the shape comes from style 4 (classic static) and every dimension is set
// explicitly; a value the game does not expose is simply absent from the block.
[[nodiscard]] std::vector<std::pair<std::string, std::string>>
crosshair_commands(const config::visual_profile::crosshair& settings);

// The same block as console text, one "name value" per line.
[[nodiscard]] std::string crosshair_command_text(const config::visual_profile::crosshair& settings);

struct crosshair_apply_result
{
    std::size_t applied{};
    std::size_t missing{};
    bool writable{false};
};

// Writes the block into the live console variables. Each value is converted to the type the variable advertises,
// so a variable that moved to another layout in a game update is skipped instead of corrupted.
[[nodiscard]] crosshair_apply_result apply_crosshair_to_game(const config::visual_profile::crosshair& settings);

} // namespace features::visuals
