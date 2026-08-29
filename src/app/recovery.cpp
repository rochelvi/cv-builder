#include "recovery.h"

#include "paths.h"
#include "settings.h"

namespace cvb {
namespace app {
namespace {

// The names earlier versions wrote, so a snapshot left by the previous build is
// still found and offered.
const char* const kOriginValue = "AutosaveOrigin";
const char* const kAutosaveName = "autosave.json";

}  // namespace

Path autosavePath() {
    const Path dir = platform::appDataDirectory();
    return dir.empty() ? Path() : dir / kAutosaveName;
}

void writeAutosave(const CV& cv, const Path& origin) {
    const Path path = autosavePath();
    if (path.empty()) return;
    std::string error;
    if (save(path, cv, error)) platform::settings::writeString(kOriginValue, toUtf8(origin));
}

void clearAutosave() {
    const Path path = autosavePath();
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    platform::settings::writeString(kOriginValue, std::string());
}

bool findRecovery(Recovery& out) {
    const Path path = autosavePath();
    if (path.empty()) return false;

    std::error_code ec;
    const auto snapshot = std::filesystem::last_write_time(path, ec);
    if (ec) return false;  // nothing was left behind

    const Path origin = fromUtf8(platform::settings::readString(kOriginValue));
    if (!origin.empty()) {
        std::error_code originEc;
        const auto saved = std::filesystem::last_write_time(origin, originEc);
        if (!originEc && snapshot <= saved) {
            clearAutosave();
            return false;
        }
    }

    std::string error;
    if (!load(path, out.cv, error)) return false;
    out.origin = origin;
    return true;
}

}  // namespace app
}  // namespace cvb
