#include "paths.h"

#include <mach-o/dyld.h>

#include <cstdlib>
#include <string>
#include <vector>

namespace cvb {
namespace platform {
namespace {

Path home() {
    if (const char* value = std::getenv("HOME")) return Path(value);
    return Path();
}

}  // namespace

Path executableDirectory() {
    uint32_t size = 0;
    // Called once to learn the length - it answers with the size it needs and a
    // non-zero result - then again to fill the buffer.
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) return Path();
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return Path();

    // The path may go through a symlink, which is how a Homebrew-style install
    // is arranged; resolve it so "next to the executable" means the real bundle.
    Path self(buffer.data());
    std::error_code ec;
    Path resolved = std::filesystem::canonical(self, ec);
    return (ec ? self : resolved).parent_path();
}

Path appDataDirectory() {
    Path base = home();
    if (base.empty()) return Path();
    Path dir = base / "Library" / "Application Support" / "CV Builder";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? Path() : dir;
}

Path documentsDirectory() {
    Path base = home();
    if (base.empty()) return Path();
    std::error_code ec;
    Path documents = base / "Documents";
    return std::filesystem::is_directory(documents, ec) ? documents : base;
}

bool samePath(const Path& a, const Path& b) {
    // The default macOS volume is case-insensitive but case-preserving, and a
    // case-sensitive one is a supported choice - so neither a byte comparison
    // nor a lowercased one is right in general. Ask the file system: when both
    // names exist it knows, and when they do not a byte comparison is all that
    // is left anyway.
    std::error_code ec;
    if (std::filesystem::exists(a, ec) && std::filesystem::exists(b, ec)) {
        bool same = std::filesystem::equivalent(a, b, ec);
        if (!ec) return same;
    }
    return a.native() == b.native();
}

Path absolutePath(const Path& path) {
    if (path.empty()) return path;
    std::error_code ec;
    Path full = std::filesystem::absolute(path, ec);
    return ec ? path : full.lexically_normal();
}

}  // namespace platform
}  // namespace cvb
