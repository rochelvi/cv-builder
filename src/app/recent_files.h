// The "recently opened" list.
//
// The policy - how many entries, what counts as the same file, what to do with
// one that has been deleted - is the same on every platform, so it lives here;
// only the storage underneath differs, and that is the platform layer's job.
#pragma once

#include <vector>

#include "file.h"

namespace cvb {
namespace app {

constexpr size_t kMaxRecentFiles = 8;

// Most recent first. Paths that no longer exist are filtered out on the way out
// rather than being offered and failing - but they are not erased from storage:
// a network drive that is currently unplugged should not silently wipe the
// history the moment it is pulled.
std::vector<Path> recentFiles();

// Stores the absolute path, so two different relative routes to one file do not
// take two slots and get opened from the wrong working directory later.
void pushRecentFile(const Path& path);

void clearRecentFiles();

}  // namespace app
}  // namespace cvb
