// The directories an application has to know about, answered by the platform.
//
// Every one of these is a different question on each operating system, and none
// of them is answerable by string arithmetic - which is why the core never asks
// them and the front ends ask here instead of reaching for a Windows header.
#pragma once

#include "file.h"

namespace cvb {
namespace platform {

// The directory holding the running executable. Used to find sample_cv.json and
// the bundled font next to the program, so a portable copy works from a stick.
// Empty if the platform will not say.
Path executableDirectory();

// Where this program may keep its own data - the crash-recovery snapshot:
//
//   Windows  %LOCALAPPDATA%\CV Builder
//   Linux    $XDG_DATA_HOME/cv-builder, else ~/.local/share/cv-builder
//   macOS    ~/Library/Application Support/CV Builder
//
// Created on demand, because the program has no install step that would do it
// and a portable copy was never installed at all. Empty when it cannot be made.
Path appDataDirectory();

// Where a save dialog should start when the document has never been saved.
Path documentsDirectory();

// True when two paths name the same file as far as this platform is concerned.
// Windows is case-insensitive and the Unixes are not, and the recent-files list
// must not hold one file twice because it was opened as C:\cv and c:\CV.
bool samePath(const Path& a, const Path& b);

// The absolute form of `path`, so two routes to one file take one slot in the
// recent list and open from the right place later. Returns `path` unchanged when
// it cannot be resolved - a network drive that is currently unplugged, say.
Path absolutePath(const Path& path);

}  // namespace platform
}  // namespace cvb
