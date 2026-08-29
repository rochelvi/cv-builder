// What a backend has to be able to draw.
//
// Three operations, because a laid-out page contains three kinds of thing: a
// filled rectangle, a straight line, and a run of glyphs. Everything above this
// - the page design, the pagination, the line breaking - has already happened;
// a Canvas only puts marks where it is told.
//
// Glyphs, not strings. The layout engine measured the page with the advance
// widths of a particular font file and the PDF writer embeds a subset of that
// same file, so a backend that took a string and asked the system to lay it out
// would be free to disagree with both. Handed glyph ids and positions, it cannot.
#pragma once

#include <cstddef>
#include <cstdint>

#include "layout.h"

namespace cvb {

// One run of glyphs, in the order they are to be drawn.
struct GlyphRun {
    const uint16_t* glyphs = nullptr;
    // Per glyph, in points, already multiplied by the point size. A backend that
    // positions glyphs itself needs these; one that lets its own text engine
    // advance through the same font file may ignore them.
    const float* advances = nullptr;
    size_t count = 0;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    // All coordinates are the document's: points from the top-left corner of the
    // sheet, y downwards. Scaling to pixels, to printer dots or to anything else
    // is the backend's business and no concern of the page.
    virtual void fillRect(double x, double y, double width, double height, RGB color) = 0;
    virtual void drawLine(double x1, double y1, double x2, double y2, double width, RGB color) = 0;

    // `y` is the baseline, which is where the layout engine puts text.
    virtual void drawGlyphs(double x, double y, double size, bool bold, const GlyphRun& run,
                            RGB color) = 0;
};

}  // namespace cvb
