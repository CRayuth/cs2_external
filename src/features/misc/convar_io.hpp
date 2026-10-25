#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace features::misc
{

// The value layout a console variable advertises in its own type tag.
enum class convar_kind : std::uint8_t
{
    unknown,
    boolean,
    integer,
    floating
};

struct convar_entry
{
    std::string name{};
    std::uintptr_t address{};
    convar_kind kind{convar_kind::unknown};
};

// Walks the live console-variable list once and types every entry that advertises a value layout Vesta can read.
// Entries without a recognised type are dropped, so nothing is written back blindly.
[[nodiscard]] std::vector<convar_entry> enumerate_convars();

// Current value in the form the console accepts ("1", "-3", "0.785"). False when the entry is stale.
[[nodiscard]] bool read_convar_text(const convar_entry& entry, std::string& text);

// Converts `text` to the entry's own type and stores it. False when the handle is read-only, the text does not
// parse, or the game no longer exposes a variable of that kind at the address.
[[nodiscard]] bool write_convar(const convar_entry& entry, std::string_view text);

} // namespace features::misc
