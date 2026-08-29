// UTF-8 to UTF-16 and back, for the Windows APIs that only speak the latter.
// Internal to the platform layer and to the Win32 front end; nothing in the core
// includes this, because nothing in the core deals in UTF-16.
#pragma once

#include <windows.h>

#include <string>

namespace cvb {
namespace platform {

inline std::wstring wideFromUtf8(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

inline std::string utf8FromWide(const std::wstring& text) {
    if (text.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0,
                                nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), n,
                        nullptr, nullptr);
    return out;
}

}  // namespace platform
}  // namespace cvb
