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

#include "palette.h"
#include "settings.h"
#include "ui.h"

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace cvb {
namespace {

// 1809 named the attribute 19; it moved to 20 in 20H1 and the old number was
// left working on the builds that shipped with it.
constexpr DWORD kImmersiveDarkModeLegacy = 19;

constexpr wchar_t kPersonalizeKey[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";

// The colours themselves live in uicommon, shared with the portable front end:
// the two are meant to be the same program, so neither may own the palette.
UiTheme fromShared(const uicommon::Palette& palette) {
    auto gdi = [](const cvb::RGB& c) { return RGB(c.r, c.g, c.b); };
    UiTheme t;
    t.window = gdi(palette.window);
    t.pane = gdi(palette.pane);
    t.card = gdi(palette.card);
    t.cardEdge = gdi(palette.cardEdge);
    t.field = gdi(palette.field);
    t.text = gdi(palette.text);
    t.subtext = gdi(palette.subtext);
    t.accent = gdi(palette.accent);
    t.previewBack = gdi(palette.previewBack);
    t.dark = palette.dark;
    return t;
}

const UiTheme kLight = fromShared(uicommon::lightPalette());
const UiTheme kDark = fromShared(uicommon::darkPalette());

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

// Only the system's own preference is read straight from the registry: that is
// a question about Windows, not a setting of ours.
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

    const int saved = platform::settings::readInt(uicommon::kModeSetting,
                                                  static_cast<int>(ThemeMode::System));
    gMode = (saved >= 0 && saved <= static_cast<int>(ThemeMode::Dark))
                ? static_cast<ThemeMode>(saved)
                : ThemeMode::System;
    recompute();
}

void setThemeMode(ThemeMode mode) {
    gMode = mode;
    recompute();
    platform::settings::writeInt(uicommon::kModeSetting, static_cast<int>(mode));
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

