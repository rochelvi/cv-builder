// The crash-recovery snapshot: written periodically while there are unsaved
// changes, removed as soon as the work is saved or the window closes cleanly.
//
// Platform-independent, like the recent-files list: where the snapshot lives is
// the platform layer's answer, but when it is worth offering is not.
#pragma once

#include <string>

#include "file.h"
#include "model.h"

namespace cvb {
namespace app {

// Inside the per-user data directory. Empty when the platform will not give one,
// in which case recovery is simply unavailable rather than fatal.
Path autosavePath();

// `origin` is the file the work belonged to, empty if it was never saved.
void writeAutosave(const CV& cv, const Path& origin);
void clearAutosave();

struct Recovery {
    CV cv;
    Path origin;
};

// True when a snapshot was left behind and is newer than the file it came from -
// that is, when the last run ended without saving. A snapshot older than its
// origin means the work was saved normally afterwards; that one is deleted
// rather than offered, since restoring it would talk the user into overwriting
// good work with worse.
bool findRecovery(Recovery& out);

}  // namespace app
}  // namespace cvb
