#pragma once

#include <string_view>

namespace platform::windows
{

// Replaces the clipboard text with `text` (UTF-8, converted to UTF-16). False when another process holds the
// clipboard or the allocation is refused; the previous contents are then left untouched.
[[nodiscard]] bool set_clipboard_text(std::string_view text) noexcept;

} // namespace platform::windows
