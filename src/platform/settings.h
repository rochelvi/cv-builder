// The handful of things the program remembers between runs, kept where the
// platform keeps such things:
//
//   Windows  HKCU\Software\CV Builder     - the same key earlier versions used,
//                                           with the same value names, so an
//                                           existing installation keeps its
//                                           theme and its recent files
//   Linux    $XDG_CONFIG_HOME/cv-builder/settings.ini
//   macOS    ~/Library/Preferences/CV Builder/settings.ini
//
// Strings are UTF-8 on every platform. What is worth remembering, and how many
// entries a list keeps, is application policy and lives above this.
#pragma once

#include <string>
#include <vector>

namespace cvb {
namespace platform {
namespace settings {

// Missing or unreadable values yield the fallback rather than an error: a
// preference nobody has set yet is not a failure.
int readInt(const char* name, int fallback);
void writeInt(const char* name, int value);

std::string readString(const char* name);
// An empty value removes the entry instead of storing emptiness.
void writeString(const char* name, const std::string& value);

std::vector<std::string> readList(const char* name);
void writeList(const char* name, const std::vector<std::string>& values);

}  // namespace settings
}  // namespace platform
}  // namespace cvb
