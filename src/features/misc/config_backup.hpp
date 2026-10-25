#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace features::misc
{

struct convar_backup_result
{
    bool ok{false};
    std::size_t variable_count{};
    std::filesystem::path path{};
};

// The current value of every typed console variable, as a CS2 "name value" block. Sets `variable_count` to the
// number of lines that carry a value.
[[nodiscard]] std::string convar_backup_text(std::size_t& variable_count);

// Writes the same block into `folder` as cs2_convars_<timestamp>.cfg. The file is created through a temporary
// sibling and an atomic rename, so an interrupted run never leaves a truncated backup.
[[nodiscard]] convar_backup_result write_convar_backup(const std::filesystem::path& folder);

} // namespace features::misc
