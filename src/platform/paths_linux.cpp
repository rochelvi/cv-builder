#include "paths.h"

#include <unistd.h>

#include <cstdlib>

namespace cvb {
namespace platform {
namespace {

Path home() {
    if (const char* value = std::getenv("HOME")) return Path(value);
    return Path();
}

// $XDG_DATA_HOME and friends are only honoured when absolute, per the spec; a
// relative value is to be ignored rather than resolved against the working
// directory.
Path xdg(const char* variable, const char* fallback) {
    const char* value = std::getenv(variable);
    if (value && value[0] == '/') return Path(value);
    Path base = home();
    return base.empty() ? Path() : base / fallback;
}

}  // namespace

Path executableDirectory() {
    std::error_code ec;
    Path self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return Path();
    return self.parent_path();
}

Path appDataDirectory() {
    Path base = xdg("XDG_DATA_HOME", ".local/share");
    if (base.empty()) return Path();
    // Lower case with a hyphen: the convention for a directory under
    // ~/.local/share, where "CV Builder" would look out of place.
    Path dir = base / "cv-builder";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? Path() : dir;
}

Path documentsDirectory() {
    // The full answer lives in ~/.config/user-dirs.dirs and is localised, which
    // is more machinery than a starting directory for a dialog deserves - and
    // the toolkit's own file dialog knows the localised name anyway. ~/Documents
    // when it exists, the home directory otherwise.
    Path base = home();
    if (base.empty()) return Path();
    std::error_code ec;
    Path documents = base / "Documents";
    return std::filesystem::is_directory(documents, ec) ? documents : base;
}

bool samePath(const Path& a, const Path& b) { return a.native() == b.native(); }

Path absolutePath(const Path& path) {
    if (path.empty()) return path;
    std::error_code ec;
    Path full = std::filesystem::absolute(path, ec);
    return ec ? path : full.lexically_normal();
}

}  // namespace platform
}  // namespace cvb
