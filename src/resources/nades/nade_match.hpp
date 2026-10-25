#pragma once

#include <resources/nades/nade_data.hpp>

#include <cstddef>
#include <string_view>
#include <utility>

namespace resources::nades
{
// Lineup tables are keyed by the plain level identifier ("de_mirage"), but the name the game reports
// is rarely that tidy: it can arrive as a vpk path, with an extension, or as a community variant of a
// map that keeps the original layout ("de_mirage_retake", "italy_smokes"). Matching only the plain
// identifier left every variant with no lineups at all, which read as a broken helper.
//
// Resolution is ordered from most to least trusted, and the outcome says whether the match was
// literal or inferred, so a caller can report an inferred one instead of presenting it as fact.

// Names longer than this only get the literal comparison. The variant search is quadratic in the
// name length, and no real level identifier comes close to this size.
inline constexpr std::size_t k_max_variant_name_bytes{128};

[[nodiscard]] inline constexpr bool map_name_boundary(const char c) noexcept
{
    return c == '_' || c == '-' || c == '+' || c == '.' || c == ' ' || c == '/' || c == '\\';
}

[[nodiscard]] inline constexpr bool ascii_letter(const char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

[[nodiscard]] inline constexpr char ascii_lower(const char c) noexcept
{
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
}

[[nodiscard]] inline constexpr bool ci_equal(const std::string_view left, const std::string_view right) noexcept
{
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (ascii_lower(left[index]) != ascii_lower(right[index]))
            return false;
    return true;
}

// Case-insensitive search for `needle` in `haystack`, honouring name boundaries on both sides, so
// "train" is found in "de_train_v2" but not in "de_shorttrain".
[[nodiscard]] inline constexpr bool ci_has_word(const std::string_view haystack, const std::string_view needle) noexcept
{
    if (needle.empty() || needle.size() > haystack.size())
        return false;
    for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start)
    {
        if (!ci_equal(haystack.substr(start, needle.size()), needle))
            continue;
        const auto left_ok = start == 0 || map_name_boundary(haystack[start - 1]);
        const auto right = start + needle.size();
        const auto right_ok = right == haystack.size() || map_name_boundary(haystack[right]);
        if (left_ok && right_ok)
            return true;
    }
    return false;
}

// Reduces whatever the level probe returned to a bare level name: the path and the extension are
// packaging, not part of the identifier.
[[nodiscard]] inline constexpr std::string_view normalize_map_name(std::string_view raw) noexcept
{
    if (const auto slash = raw.find_last_of("/\\"); slash != std::string_view::npos)
        raw.remove_prefix(slash + 1);
    if (const auto dot = raw.find('.'); dot != std::string_view::npos)
        raw = raw.substr(0, dot);
    return raw;
}

// Drops the two-letter type prefix ("de_", "cs_", "ar_") so a variant that omits or swaps it still
// resolves: community remakes of Italy appear as "cs_italy_gg", "de_italy_gg" and "italy_gg" alike.
// Only a genuinely two-letter leading token is removed, so "italy_gg" keeps "italy" in its stem.
[[nodiscard]] inline constexpr std::string_view map_stem(const std::string_view name) noexcept
{
    if (name.size() > 3 && name[2] == '_' && ascii_letter(name[0]) && ascii_letter(name[1]))
        return name.substr(3);
    return name;
}

// Spellings that share no whole word with their table key, so no variant rule would reach them.
inline constexpr std::pair<std::string_view, std::string_view> k_map_aliases[]{
    { "de_dust_2", "de_dust2" },
};

struct map_resolution
{
    const map_entries* map{};
    // The table key that matched, which is what an overlay file must be named after.
    std::string_view name{};
    // True when the level name was only related to the key (an alias or a variant) rather than equal
    // to it. Markers from a related table can still be wrong, so this is reported, not hidden.
    bool approximate{};
};

// Resolves a level name to a lineup table: literal key first, then a known alternative spelling, then
// a variant sharing a whole word with the key or with the key's stem. The longest key wins, so a name
// mentioning two maps resolves to the more specific table.
[[nodiscard]] inline constexpr map_resolution resolve_map(const std::string_view raw) noexcept
{
    const auto name = normalize_map_name(raw);
    if (name.empty())
        return {};

    for (const auto& candidate : k_maps)
    {
        if (ci_equal(name, candidate.map))
            return { &candidate, candidate.map, false };
    }

    for (const auto& [spelling, canonical] : k_map_aliases)
    {
        if (!ci_equal(name, spelling))
            continue;
        for (const auto& candidate : k_maps)
        {
            if (ci_equal(candidate.map, canonical))
                return { &candidate, candidate.map, true };
        }
    }

    if (name.size() > k_max_variant_name_bytes)
        return {};

    const auto stem = map_stem(name);
    const map_entries* best{};
    std::size_t best_size{};
    for (const auto& candidate : k_maps)
    {
        // The table stores plain names as char pointers; only the comparison wants a view.
        const std::string_view key{candidate.map};
        const auto key_stem = map_stem(key);
        const auto matched = ci_has_word(name, key) || (key_stem != key && ci_has_word(stem, key_stem));
        if (matched && key.size() > best_size)
        {
            best = &candidate;
            best_size = key.size();
        }
    }
    if (best)
        return { best, best->map, true };
    return {};
}

} // namespace resources::nades
