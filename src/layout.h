// Turns a CV into positioned drawing primitives - the single source of truth
// for the page design.
//
// Nothing here knows about PDF or about windows: it emits pages of text runs,
// rules and fills in points, measured from the top-left corner of the sheet.
// The PDF writer and the on-screen preview are both just backends for this
// list, which is why they cannot drift apart.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "font.h"
#include "model.h"

namespace cvb {

struct RGB {
    uint8_t r = 0, g = 0, b = 0;
};

// Accepts "#rrggbb" and bare "rrggbb"; anything unparseable yields `fallback`,
// so a half-typed colour in the editor never breaks the render.
RGB parseColor(const std::string& text, RGB fallback);

struct TextItem {
    double x = 0, y = 0;  // y is the baseline, measured down from the page top
    double size = 9;
    bool bold = false;
    RGB color;
    std::string text;  // UTF-8
};

struct LineItem {
    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
    double width = 0.5;
    RGB color;
};

struct RectItem {
    double x = 0, y = 0, w = 0, h = 0;
    RGB color;
};

struct Page {
    RectItem background;
    std::vector<LineItem> lines;
    std::vector<TextItem> texts;
};

struct Document {
    double width = 0, height = 0;  // A4, in points
    std::vector<Page> pages;
    std::string title;
    std::string author;
};

// Page geometry, kept public so the preview can size its canvas.
extern const double kPageWidth;
extern const double kPageHeight;

Document layout(const CV& cv, const FontSet& fonts);

}  // namespace cvb
