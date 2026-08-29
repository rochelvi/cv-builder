// Everything the app remembers between runs, apart from the theme: the list of
// recently opened files and the crash-recovery snapshot.
//
// The list lives in the registry next to the theme, because it is a handful of
// short strings that nobody should have to manage as a file. The snapshot is a
// file, because it holds a whole CV and gets rewritten every half minute.
#include <windows.h>

#include <shlobj.h>

#include <algorithm>

#include "model.h"
#include "ui.h"

namespace cvb {
namespace {

constexpr wchar_t kRecentValue[] = L"RecentFiles";
constexpr wchar_t kAutosaveOriginValue[] = L"AutosaveOrigin";
constexpr wchar_t kAutosaveName[] = L"autosave.json";

// REG_MULTI_SZ: one value holding the whole list, rather than Recent1..Recent10
// that have to be renumbered every time something moves to the front.
std::vector<std::wstring> readMultiSz(const wchar_t* value) {
    std::vector<std::wstring> out;
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value, RRF_RT_REG_MULTI_SZ, nullptr, nullptr,
                     &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
        return out;

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value, RRF_RT_REG_MULTI_SZ, nullptr,
                     buffer.data(), &size) != ERROR_SUCCESS)
        return out;

    for (const wchar_t* p = buffer.data(); *p; p += wcslen(p) + 1) out.emplace_back(p);
    return out;
}

void writeMultiSz(const wchar_t* value, const std::vector<std::wstring>& items) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;

    if (items.empty()) {
        RegDeleteValueW(key, value);
        RegCloseKey(key);
        return;
    }

    // A MULTI_SZ is the strings back to back, each terminated, then one more
    // terminator to close the block.
    std::vector<wchar_t> blob;
    for (const std::wstring& item : items) {
        blob.insert(blob.end(), item.begin(), item.end());
        blob.push_back(L'\0');
    }
    blob.push_back(L'\0');
    RegSetValueExW(key, value, 0, REG_MULTI_SZ, reinterpret_cast<const BYTE*>(blob.data()),
                   static_cast<DWORD>(blob.size() * sizeof(wchar_t)));
    RegCloseKey(key);
}

std::wstring readString(const wchar_t* value) {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof buffer;
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, value, RRF_RT_REG_SZ, nullptr, buffer,
                     &size) != ERROR_SUCCESS)
        return std::wstring();
    return std::wstring(buffer);
}

void writeString(const wchar_t* value, const std::wstring& text) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kSettingsKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;
    if (text.empty())
        RegDeleteValueW(key, value);
    else
        RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(text.c_str()),
                       static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
}

// Case-insensitive, because Windows paths are: the same file opened as C:\cv
// and c:\CV must not appear twice in the list.
bool samePath(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() &&
           CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(),
                                static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

FILETIME modifiedAt(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return FILETIME{};
    return info.ftLastWriteTime;
}

bool isZero(const FILETIME& t) { return t.dwLowDateTime == 0 && t.dwHighDateTime == 0; }

}  // namespace

// ------------------------------------------------------------ recent files

std::vector<std::wstring> recentFiles() {
    std::vector<std::wstring> files = readMultiSz(kRecentValue);
    // Entries that no longer exist are dropped on the way out rather than
    // being offered and failing: a list of dead paths is worse than a short
    // list. They are not rewritten here - a missing network drive should not
    // silently erase the history the moment it is unplugged.
    files.erase(std::remove_if(files.begin(), files.end(),
                               [](const std::wstring& path) {
                                   return GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES;
                               }),
                files.end());
    if (files.size() > kMaxRecentFiles) files.resize(kMaxRecentFiles);
    return files;
}

void pushRecentFile(const std::wstring& path) {
    if (path.empty()) return;

    // Store the full, resolved path: two different relative routes to one file
    // would otherwise take two slots and open one of them from the wrong
    // working directory later.
    wchar_t full[1024] = {};
    DWORD n = GetFullPathNameW(path.c_str(), 1024, full, nullptr);
    std::wstring canonical = (n > 0 && n < 1024) ? std::wstring(full, n) : path;

    std::vector<std::wstring> files = readMultiSz(kRecentValue);
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const std::wstring& item) { return samePath(item, canonical); }),
                files.end());
    files.insert(files.begin(), canonical);
    if (files.size() > kMaxRecentFiles) files.resize(kMaxRecentFiles);
    writeMultiSz(kRecentValue, files);
}

void clearRecentFiles() { writeMultiSz(kRecentValue, {}); }

// --------------------------------------------------------------- autosave

std::wstring autosavePath() {
    PWSTR local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
    std::wstring dir = std::wstring(local) + L"\\" + kAppFolderName;
    CoTaskMemFree(local);
    // Created on demand: the app has no installer step that would do it, and a
    // portable copy has never been installed at all.
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + kAutosaveName;
}

void writeAutosave(const CV& cv, const std::wstring& origin) {
    const std::wstring path = autosavePath();
    if (path.empty()) return;
    std::string error;
    if (save(path, cv, error)) writeString(kAutosaveOriginValue, origin);
}

void clearAutosave() {
    const std::wstring path = autosavePath();
    if (!path.empty()) DeleteFileW(path.c_str());
    writeString(kAutosaveOriginValue, std::wstring());
}

bool findRecovery(Recovery& out) {
    const std::wstring path = autosavePath();
    if (path.empty()) return false;
    const FILETIME snapshot = modifiedAt(path);
    if (isZero(snapshot)) return false;  // nothing was left behind

    const std::wstring origin = readString(kAutosaveOriginValue);
    if (!origin.empty()) {
        // A snapshot older than the file it came from means the work was saved
        // normally afterwards and the snapshot is stale; offering it would
        // invite the user to overwrite good work with worse.
        const FILETIME saved = modifiedAt(origin);
        if (!isZero(saved) && CompareFileTime(&snapshot, &saved) <= 0) {
            clearAutosave();
            return false;
        }
    }

    std::string error;
    if (!load(path, out.cv, error)) return false;
    out.origin = origin;
    out.savedAt = snapshot;
    return true;
}

}  // namespace cvb
