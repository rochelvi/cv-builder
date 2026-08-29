// Settings in the registry, under the key earlier versions of the program used,
// with the same value names and the same types. An existing installation keeps
// its theme choice and its recent files across this refactor, and the
// uninstaller entry in setup.iss still removes everything we wrote.
#include <windows.h>

#include <cstring>

#include "settings.h"
#include "text_windows.h"

namespace cvb {
namespace platform {
namespace settings {
namespace {

const wchar_t kKey[] = L"Software\\CV Builder";

// Opened for writing on demand and closed straight away: these are a handful of
// short values written when the user changes something, not a hot path.
struct WriteKey {
    HKEY handle = nullptr;

    WriteKey() {
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                            &handle, nullptr) != ERROR_SUCCESS)
            handle = nullptr;
    }
    ~WriteKey() {
        if (handle) RegCloseKey(handle);
    }
    explicit operator bool() const { return handle != nullptr; }
};

}  // namespace

int readInt(const char* name, int fallback) {
    const std::wstring value = wideFromUtf8(name);
    DWORD data = 0;
    DWORD size = sizeof data;
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, value.c_str(), RRF_RT_REG_DWORD, nullptr, &data,
                     &size) != ERROR_SUCCESS)
        return fallback;
    return static_cast<int>(data);
}

void writeInt(const char* name, int value) {
    WriteKey key;
    if (!key) return;
    DWORD data = static_cast<DWORD>(value);
    RegSetValueExW(key.handle, wideFromUtf8(name).c_str(), 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&data), sizeof data);
}

std::string readString(const char* name) {
    const std::wstring value = wideFromUtf8(name);
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, value.c_str(), RRF_RT_REG_SZ, nullptr, nullptr,
                     &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
        return std::string();

    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, value.c_str(), RRF_RT_REG_SZ, nullptr, buffer.data(),
                     &size) != ERROR_SUCCESS)
        return std::string();
    buffer.resize(std::wcslen(buffer.c_str()));
    return utf8FromWide(buffer);
}

void writeString(const char* name, const std::string& value) {
    WriteKey key;
    if (!key) return;
    const std::wstring wideName = wideFromUtf8(name);
    if (value.empty()) {
        RegDeleteValueW(key.handle, wideName.c_str());
        return;
    }
    const std::wstring data = wideFromUtf8(value);
    RegSetValueExW(key.handle, wideName.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(data.c_str()),
                   static_cast<DWORD>((data.size() + 1) * sizeof(wchar_t)));
}

// REG_MULTI_SZ: one value holding the whole list, rather than Recent1..Recent8
// that have to be renumbered every time something moves to the front.
std::vector<std::string> readList(const char* name) {
    std::vector<std::string> out;
    const std::wstring value = wideFromUtf8(name);
    DWORD size = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, value.c_str(), RRF_RT_REG_MULTI_SZ, nullptr, nullptr,
                     &size) != ERROR_SUCCESS || size < sizeof(wchar_t))
        return out;

    std::vector<wchar_t> buffer(size / sizeof(wchar_t) + 1, L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kKey, value.c_str(), RRF_RT_REG_MULTI_SZ, nullptr,
                     buffer.data(), &size) != ERROR_SUCCESS)
        return out;

    for (const wchar_t* p = buffer.data(); *p; p += std::wcslen(p) + 1)
        out.push_back(utf8FromWide(p));
    return out;
}

void writeList(const char* name, const std::vector<std::string>& values) {
    WriteKey key;
    if (!key) return;
    const std::wstring wideName = wideFromUtf8(name);
    if (values.empty()) {
        RegDeleteValueW(key.handle, wideName.c_str());
        return;
    }

    // A MULTI_SZ is the strings back to back, each terminated, then one more
    // terminator to close the block.
    std::vector<wchar_t> blob;
    for (const std::string& item : values) {
        const std::wstring wide = wideFromUtf8(item);
        blob.insert(blob.end(), wide.begin(), wide.end());
        blob.push_back(L'\0');
    }
    blob.push_back(L'\0');
    RegSetValueExW(key.handle, wideName.c_str(), 0, REG_MULTI_SZ,
                   reinterpret_cast<const BYTE*>(blob.data()),
                   static_cast<DWORD>(blob.size() * sizeof(wchar_t)));
}

}  // namespace settings
}  // namespace platform
}  // namespace cvb
