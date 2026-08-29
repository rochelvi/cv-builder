// Draws a laid-out page onto a Canvas.
//
// One traversal, shared by every backend that is not the PDF writer: the screen
// preview, the printer, and whatever comes next. That sharing is the point - the
// order things are painted in, and the glyphs each run is turned into, are
// decided once here rather than once per backend, so two backends cannot
// disagree about what the page contains.
//
// The PDF writer stays separate on purpose: it does not paint pixels, it writes
// content-stream operators, and it needs the glyph set of the whole document up
// front to build the embedded subset.
#pragma once

#include <cstdint>
#include <vector>

#include "canvas.h"
#include "font.h"
#include "layout.h"

namespace cvb {

// Holds the shaping buffers between calls, so redrawing a page - which the
// preview does on every keystroke and on every scroll notch - allocates nothing.
class DocumentPainter {
public:
    DocumentPainter() = default;
    explicit DocumentPainter(const FontSet& fonts) : fonts_(&fonts) {}

    // A front end learns which fonts it has after its window exists, so the
    // painter can be made first and pointed at them later. Painting without them
    // draws nothing rather than crashing.
    void setFonts(const FontSet* fonts) { fonts_ = fonts; }

    void paint(const Page& page, Canvas& canvas);

private:
    const FontSet* fonts_ = nullptr;
    std::vector<uint16_t> glyphs_;
    std::vector<float> advances_;
};

}  // namespace cvb
