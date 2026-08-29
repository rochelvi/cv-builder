// Where this operating system keeps its fonts, and which of them are worth
// trying for this document.
//
// The core parses fonts and measures with them; finding one is a question only
// the platform can answer, and the answer differs enough between the three that
// it cannot be a list of file names shared by all.
//
// Order matters and is not cosmetic. The template was designed against
// Helvetica metrics, so the line breaks a resume already has depend on the
// advance widths of whatever face is used. A face further down this list is a
// worse match, and the bundled font - which the application layer tries before
// any of these - is the only way to be sure.
#pragma once

#include <vector>

#include "font.h"

namespace cvb {
namespace platform {

std::vector<FontChoice> systemFontCandidates();

}  // namespace platform
}  // namespace cvb
