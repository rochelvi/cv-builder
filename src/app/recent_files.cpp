#include "recent_files.h"

#include <algorithm>
#include <string>

#include "paths.h"
#include "settings.h"

namespace cvb {
namespace app {
namespace {

// The value name earlier versions wrote, so an existing list survives.
const char* const kRecentValue = "RecentFiles";

}  // namespace

std::vector<Path> recentFiles() {
    std::vector<Path> out;
    for (const std::string& item : platform::settings::readList(kRecentValue)) {
        Path path = fromUtf8(item);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) continue;
        out.push_back(std::move(path));
        if (out.size() >= kMaxRecentFiles) break;
    }
    return out;
}

void pushRecentFile(const Path& path) {
    if (path.empty()) return;
    const Path canonical = platform::absolutePath(path);

    std::vector<std::string> items = platform::settings::readList(kRecentValue);
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const std::string& item) {
                                   return platform::samePath(fromUtf8(item), canonical);
                               }),
                items.end());
    items.insert(items.begin(), toUtf8(canonical));
    if (items.size() > kMaxRecentFiles) items.resize(kMaxRecentFiles);
    platform::settings::writeList(kRecentValue, items);
}

void clearRecentFiles() { platform::settings::writeList(kRecentValue, {}); }

}  // namespace app
}  // namespace cvb
