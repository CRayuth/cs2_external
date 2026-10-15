#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace game
{
class offsets_catalog
{
public:
    bool load();
    bool load_from(const std::filesystem::path& file);

    [[nodiscard]] std::uintptr_t rva(std::string_view key) const;
    [[nodiscard]] std::size_t entry_count() const;
    [[nodiscard]] std::int32_t build_number() const;
    [[nodiscard]] std::filesystem::path location() const;
    [[nodiscard]] std::string status_text() const;

private:
    mutable std::shared_mutex m_mutex{};
    std::unordered_map<std::string, std::uintptr_t> m_entries{};
    std::filesystem::path m_path{};
    std::string m_error{};
    std::int32_t m_build_number{-1};
    std::chrono::system_clock::time_point m_loaded_at{};
};

inline offsets_catalog& offsets()
{
    static offsets_catalog value{};
    return value;
}

} // namespace game
