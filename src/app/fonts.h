// Choosing the pair of faces the whole document is measured, drawn and embedded
// with.
//
// The order is the point of this file. The template's line breaks are a function
// of the advance widths of the face in use, so a resume laid out with one face
// and re-opened with another can wrap differently and change how many pages it
// takes. Two faces being "metric-compatible" is a claim about a design, not a
// guarantee about the files installed on a particular machine.
//
// So: the font that ships with the program is tried first, on every platform,
// and that is what makes a PDF exported on Linux the same file as one exported
// on Windows. The system faces are a fallback for a build without the bundled
// asset - useful, and explicitly not equivalent.
#pragma once

#include <string>
#include <vector>

#include "file.h"
#include "font.h"

namespace cvb {
namespace app {

// Where the bundled faces are looked for, in order: next to the executable, in
// the assets directory beside it, in the platform's share directory for an
// installed copy, and in the source tree for a build run from the tree.
std::vector<Path> bundledFontRoots();

// Bundled first, system faces after. Fails only when nothing at all can be read.
bool loadFonts(FontSet& out, std::string& error);

// True when the pair in use is the bundled one - which is to say, when the
// output is the reference output rather than an approximation of it. The status
// line says so, and the tests assert on it.
bool usingBundledFonts(const FontSet& fonts);

}  // namespace app
}  // namespace cvb
