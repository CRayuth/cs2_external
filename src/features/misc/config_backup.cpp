#include <stdafx.hpp>
#include <features/misc/config_backup.hpp>
#include <features/misc/convar_io.hpp>
#include <platform/windows/storage.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <format>
#include <string>
#include <vector>

namespace features::misc
{

namespace
{
[[nodiscard]] std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (::localtime_s(&local, &seconds) != 0)
        return "00000000_000000";
    return std::format("{:04}{:02}{:02}_{:02}{:02}{:02}", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                       local.tm_hour, local.tm_min, local.tm_sec);
}
} // namespace

std::string convar_backup_text(std::size_t& variable_count)
{
    variable_count = 0;

    auto entries = enumerate_convars();
    // A stable order makes two backups diffable, which is the point of keeping them.
    std::ranges::sort(entries, {}, &convar_entry::name);

    std::string text{};
    text += "// CS2 console variables backed up by Vesta. Load with: exec <file>\n";
    text += "// Lines hold the live value at the moment the backup was taken.\n\n";

    for (const auto& entry : entries)
    {
        std::string value{};
        if (!read_convar_text(entry, value))
            continue;
        text += std::format("{} \"{}\"\n", entry.name, value);
        ++variable_count;
    }
    return text;
}

convar_backup_result write_convar_backup(const std::filesystem::path& folder)
{
    convar_backup_result result{};
    if (folder.empty())
        return result;

    const auto text = convar_backup_text(result.variable_count);
    if (result.variable_count == 0)
        return result;

    result.path = folder / std::format("cs2_convars_{}.cfg", timestamp());
    const auto write_error =
        platform::windows::write_file_atomically(platform::windows::data_storage::extended_path(result.path), text);
    if (write_error)
    {
        result.ok = false;
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace features::misc
