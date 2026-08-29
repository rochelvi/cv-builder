// Light and dark palettes, the Windows preference behind them, and the Win32
// plumbing that makes the standard controls follow along.
//
// Dark mode for common controls is not public API: uxtheme.dll exports the
// switches by ordinal only, and they exist from Windows 10 1809 onwards. Every
// call is therefore looked up at run time and simply skipped on older systems,
// where the app stays light whatever the user picks.
// windows.h first: the other SDK headers assume its basic types exist.
#include <windows.h>

#include <dwmapi.h>
#include <uxtheme.h>

#include <cwchar>
#include <iterator>

#include "ui.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace cvb {
// Shared with settings.cpp: one registry key holds everything the app
// remembers between runs.
const wchar_t kSettingsKey[] = L"Software\\CV Builder";
const wchar_t kAppFolderName[] = L"CV Builder";

namespace {

// 1809 named the attribute 19; it moved to 20 in 20H1 and the old number was
// left working on the builds that shipped with it.
constexpr DWORD kImmersiveDarkModeLegacy = 19;

constexpr wchar_t kSettingsValue[] = L"ThemeMode";
constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

UiTheme darkPalette() {
    UiTheme t;
    t.window = RGB(0x20, 0x20, 0x20);
    t.pane = RGB(0x1B, 0x1B, 0x1B);
    t.card = RGB(0x2A, 0x2B, 0x2E);
    t.cardEdge = RGB(0x3D, 0x3F, 0x44);
    t.field = RGB(0x2D, 0x2E, 0x31);
    t.text = RGB(0xE6, 0xE8, 0xEA);
    t.subtext = RGB(0x9A, 0xA1, 0xAC);
    t.accent = RGB(0x4C, 0x9B, 0xF5);
    t.previewBack = RGB(0x2A, 0x2C, 0x2F);
    t.dark = true;
    return t;
}

const UiTheme kLight;
const UiTheme kDark = darkPalette();

UiTheme gTheme = kLight;
ThemeMode gMode = ThemeMode::System;
bool gSupported = false;  // the undocumented dark-mode switches are usable

// ------------------------------------------------- undocumented uxtheme API
enum PreferredAppMode { PAM_Default = 0, PAM_AllowDark = 1, PAM_ForceDark = 2, PAM_ForceLight = 3 };
using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);
using RtlGetNtVersionNumbersFn = void(WINAPI*)(DWORD*, DWORD*, DWORD*);

SetPreferredAppModeFn gSetPreferredAppMode = nullptr;
AllowDarkModeForWindowFn gAllowDarkModeForWindow = nullptr;

// GetVersionEx lies to unmanifested and manifested apps alike; ntdll does not.
DWORD windowsBuild() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return 0;
    auto get = reinterpret_cast<RtlGetNtVersionNumbersFn>(
        reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetNtVersionNumbers")));
    if (!get) return 0;
    DWORD major = 0, minor = 0, build = 0;
    get(&major, &minor, &build);
    build &= ~0xF0000000;  // the top nibble is a "checked build" marker
    return major >= 10 ? build : 0;
}

DWORD readDword(HKEY root, const wchar_t* key, const wchar_t* value, DWORD fallback) {
    DWORD data = fallback;
    DWORD size = sizeof data;
    DWORD type = 0;
    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, &type, &data, &size) != ERROR_SUCCESS)
        return fallback;
    return data;
}

bool systemPrefersDark() {
    // Missing value means light: that is what a fresh install looks like.
    return readDword(HKEY_CURRENT_USER, kPersonalizeKey, L"AppsUseLightTheme", 1) == 0;
}

void recompute() {
    const bool dark = gMode == ThemeMode::Dark ||
                      (gMode == ThemeMode::System && systemPrefersDark());
    gTheme = (dark && gSupported) ? kDark : kLight;
}

void themeControl(HWND control, bool dark) {
    if (gAllowDarkModeForWindow) gAllowDarkModeForWindow(control, dark ? TRUE : FALSE);

    wchar_t cls[64] = {};
    GetClassNameW(control, cls, static_cast<int>(std::size(cls)));
    // "DarkMode_CFD" is what darkens the frame of a drop-down. Everything
    // else, an edit box included, wants "DarkMode_Explorer": it draws the same
    // frame and is the only one that also darkens a control's own scroll bars.
    const bool combo = _wcsicmp(cls, L"ComboBox") == 0;
    SetWindowTheme(control, dark ? (combo ? L"DarkMode_CFD" : L"DarkMode_Explorer") : nullptr,
                   nullptr);

    // A drop-down is three windows; the two inner ones need the same treatment
    // or the list stays white under a dark box.
    if (!combo) return;
    COMBOBOXINFO info{};
    info.cbSize = sizeof info;
    if (!GetComboBoxInfo(control, &info)) return;
    for (HWND part : {info.hwndList, info.hwndItem}) {
        if (!part || part == control) continue;
        if (gAllowDarkModeForWindow) gAllowDarkModeForWindow(part, dark ? TRUE : FALSE);
        SetWindowTheme(part, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    }
}

BOOL CALLBACK themeChild(HWND child, LPARAM param) {
    themeControl(child, param != 0);
    return TRUE;
}

}  // namespace

const UiTheme& ui() { return gTheme; }

void applyThemeToControl(HWND control) { themeControl(control, gTheme.dark); }

ThemeMode themeMode() { return gMode; }

void initTheme() {
    gSupported = windowsBuild() >= 17763;
    if (gSupported) {
        HMODULE uxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (uxtheme) {
            gSetPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
                reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135))));
            gAllowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(
                reinterpret_cast<void*>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(133))));
        }
        // Ordinal 135 was AllowDarkModeForApp(BOOL) in 1809 and became
        // SetPreferredAppMode in 1903. Both read their argument as an int and
        // treat 1 as "dark is allowed", so one call covers the two shapes.
        if (gSetPreferredAppMode) gSetPreferredAppMode(PAM_AllowDark);
    }

    DWORD saved = readDword(HKEY_CURRENT_USER, kSettingsKey, kSettingsValue,
                            static_cast<DWORD>(ThemeMode::System));
    gMode = saved <= static_cast<DWORD>(ThemeMode::Dark) ? static_cast<ThemeMode>(saved)
                                                         : ThemeMode::System;
    recompute();
}

void setThemeMode(ThemeMode mode) {
    gMode = mode;
    recompute();

    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;
    DWORD value = static_cast<DWORD>(mode);
    RegSetValueExW(key, kSettingsValue, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                   sizeof value);
    RegCloseKey(key);
}

void refreshTheme() { recompute(); }

void applyThemeToWindow(HWND window) {
    const bool dark = gTheme.dark;
    if (gSupported) {
        BOOL flag = dark ? TRUE : FALSE;
        if (FAILED(DwmSetWindowAttribute(window, DWMWA_USE_IMMERSIVE_DARK_MODE, &flag,
                                         sizeof flag)))
            DwmSetWindowAttribute(window, kImmersiveDarkModeLegacy, &flag, sizeof flag);
    }
    themeControl(window, dark);
    EnumChildWindows(window, themeChild, dark ? 1 : 0);
    // The title bar only picks up the new colour once the frame is recomputed.
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

}  // namespace cvb
