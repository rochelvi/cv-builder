// Writes a laid-out document as a PDF 1.4 file.
//
// Self-contained: no PDF library, no compression library. Text is emitted as
// Identity-H glyph runs against a subset of the system font embedded in the
// file, so the result renders identically on a machine that has never heard of
// Arial, and Cyrillic survives copy-paste through the ToUnicode map.
#pragma once

#include <string>
#include <vector>

#include "font.h"
#include "layout.h"

namespace cvb {

// The finished PDF bytes. Empty on failure, with `error` set.
std::vector<uint8_t> buildPdf(const Document& doc, const FontSet& fonts, std::string& error);

bool writePdf(const Document& doc, const FontSet& fonts, const Path& path, std::string& error);

}  // namespace cvb
