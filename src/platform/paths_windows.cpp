#include "paths.h"

// windows.h first: the other SDK headers assume its basic types exist.
#include <windows.h>

#include <shlobj.h>

#include <string>

namespace cvb {
namespace platform {
namespace {

// The folder ids are resolved through the shell rather than by reading
// %LOCALAPPDATA%: a redirected or roaming profile answers correctly this way and
// not the other.
Path knownFolder(const KNOWNFOLDERID& id) {
    PWSTR wide = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, 0, nullptr, &wide))) return Path();
    Path path(wide);
    CoTaskMemFree(wide);
    return path;
}

}  // namespace

Path executableDirectory() {
    // MAX_PATH is not the limit for a path on a modern Windows, so the buffer
    // grows until the name fits rather than being silently truncated into a
    // directory that does not exist.
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (n == 0) return Path();
        if (n < buffer.size()) {
            buffer.resize(n);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return Path(buffer).parent_path();
}

Path appDataDirectory() {
    Path base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) return Path();
    Path dir = base / L"CV Builder";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? Path() : dir;
}

Path documentsDirectory() { return knownFolder(FOLDERID_Documents); }

bool samePath(const Path& a, const Path& b) {
    const std::wstring left = a.native();
    const std::wstring right = b.native();
    if (left.size() != right.size()) return false;
    // Ordinal, case-insensitive: what the file system itself does, without the
    // locale-dependent collation a CompareString without this flag would apply.
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

Path absolutePath(const Path& path) {
    if (path.empty()) return path;
    std::error_code ec;
    Path full = std::filesystem::absolute(path, ec);
    return ec ? path : full.lexically_normal();
}

}  // namespace platform
}  // namespace cvb
