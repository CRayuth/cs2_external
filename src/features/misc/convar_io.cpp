#include <stdafx.hpp>
#include <features/misc/convar_io.hpp>
#include <game/memory/bound_services.hpp>
#include <game/memory/compatibility_contracts.hpp>
#include <game/state/services.hpp>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>

namespace features::misc
{

namespace
{
// ConVarData: the live value sits at +0x58 and the default at +0x60. Reading 0x5c bytes covers the value and the
// type tag the compatibility contract validates at +0x28.
constexpr std::size_t k_snapshot_bytes = 0x5c;
constexpr std::uintptr_t k_value_offset = 0x58;

using value_buffer = std::array<std::byte, k_snapshot_bytes>;

// The three value layouts are distinguishable by their type tag, and exactly one of the typed readers accepts it.
[[nodiscard]] convar_kind classify(const value_buffer& bytes)
{
    const std::span<const std::byte> span{bytes};
    if (bool boolean{}; game::compatibility::detail::convar_value(span, boolean))
        return convar_kind::boolean;
    if (int integer{}; game::compatibility::detail::convar_value(span, integer))
        return convar_kind::integer;
    if (float real{}; game::compatibility::detail::convar_value(span, real))
        return convar_kind::floating;
    return convar_kind::unknown;
}

[[nodiscard]] bool read_bytes(const std::uintptr_t address, value_buffer& bytes)
{
    return game::bound_memory().process.copy(address, bytes.data(), bytes.size());
}

[[nodiscard]] bool format_value(const value_buffer& bytes, std::string& text)
{
    const std::span<const std::byte> span{bytes};
    if (bool boolean{}; game::compatibility::detail::convar_value(span, boolean))
    {
        text = boolean ? "1" : "0";
        return true;
    }
    if (int integer{}; game::compatibility::detail::convar_value(span, integer))
    {
        text = std::format("{}", integer);
        return true;
    }
    if (float real{}; game::compatibility::detail::convar_value(span, real))
    {
        // Six significant digits keep positions, sensitivities and times intact without dragging a 1.200000047
        // tail into the file.
        text = std::format("{:.6g}", static_cast<double>(real));
        return true;
    }
    return false;
}

[[nodiscard]] bool parse_number(const std::string_view text, double& value)
{
    auto normalized = std::string{text};
    const auto first = normalized.find_first_not_of(" \t");
    if (first == std::string::npos)
        return false;
    const auto last = normalized.find_last_not_of(" \t");
    normalized = normalized.substr(first, last - first + 1);
    if (normalized.empty())
        return false;

    char* end{};
    const auto parsed = std::strtod(normalized.c_str(), &end);
    if (end != normalized.c_str() + normalized.size() || !std::isfinite(parsed))
        return false;
    value = parsed;
    return true;
}
} // namespace

std::vector<convar_entry> enumerate_convars()
{
    std::vector<convar_entry> entries{};
    for (auto& variable : game::variables().snapshot())
    {
        value_buffer bytes{};
        if (!read_bytes(variable.address, bytes))
            continue;
        const auto kind = classify(bytes);
        if (kind == convar_kind::unknown)
            continue;
        entries.push_back(convar_entry{std::move(variable.name), variable.address, kind});
    }
    return entries;
}

bool read_convar_text(const convar_entry& entry, std::string& text)
{
    value_buffer bytes{};
    if (!entry.address || !read_bytes(entry.address, bytes))
        return false;
    return format_value(bytes, text);
}

bool write_convar(const convar_entry& entry, const std::string_view text)
{
    auto& process = game::bound_memory().process;
    if (!entry.address || !process.writable())
        return false;

    value_buffer bytes{};
    if (!read_bytes(entry.address, bytes))
        return false;

    double number{};
    if (!parse_number(text, number))
        return false;

    switch (classify(bytes))
    {
        case convar_kind::boolean:
            if (number != 0.0 && number != 1.0)
                return false;
            return process.store<bool>(entry.address + k_value_offset, number != 0.0);
        case convar_kind::integer:
            if (number < static_cast<double>(std::numeric_limits<int>::min()) ||
                number > static_cast<double>(std::numeric_limits<int>::max()))
                return false;
            return process.store<int>(entry.address + k_value_offset, static_cast<int>(std::lround(number)));
        case convar_kind::floating:
            return process.store<float>(entry.address + k_value_offset, static_cast<float>(number));
        case convar_kind::unknown:
        default:
            return false;
    }
}

} // namespace features::misc
