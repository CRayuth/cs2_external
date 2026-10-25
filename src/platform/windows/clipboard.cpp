#include <stdafx.hpp>
#include <platform/windows/clipboard.hpp>
#include <windows.h>
#include <cstring>
#include <string>

namespace platform::windows
{

namespace
{
[[nodiscard]] std::wstring to_wide(const std::string_view text)
{
    if (text.empty())
        return {};
    const auto length = ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length) != length)
        return {};
    return wide;
}
} // namespace

bool set_clipboard_text(const std::string_view text) noexcept
{
    const auto wide = to_wide(text);
    if (wide.empty() && !text.empty())
        return false;

    if (!::OpenClipboard(nullptr))
        return false;

    if (!::EmptyClipboard())
    {
        ::CloseClipboard();
        return false;
    }

    const auto bytes = (wide.size() + 1) * sizeof(wchar_t);
    const auto memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
    {
        ::CloseClipboard();
        return false;
    }

    const auto destination = ::GlobalLock(memory);
    if (!destination)
    {
        ::GlobalFree(memory);
        ::CloseClipboard();
        return false;
    }
    std::memcpy(destination, wide.c_str(), bytes);
    ::GlobalUnlock(memory);

    const auto accepted = ::SetClipboardData(CF_UNICODETEXT, memory) != nullptr;
    if (!accepted)
        ::GlobalFree(memory);
    ::CloseClipboard();
    return accepted;
}

} // namespace platform::windows
