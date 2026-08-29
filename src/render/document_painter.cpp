#include "document_painter.h"

namespace cvb {

void DocumentPainter::paint(const Page& page, Canvas& canvas) {
    if (!fonts_) return;

    // Background, then rules, then text - the order the PDF writer emits them
    // in, and the only order in which the rules sit on the paper and under the
    // words rather than over them.
    canvas.fillRect(page.background.x, page.background.y, page.background.w, page.background.h,
                    page.background.color);

    for (const LineItem& line : page.lines)
        canvas.drawLine(line.x1, line.y1, line.x2, line.y2, line.width, line.color);

    for (const TextItem& item : page.texts) {
        const Font& font = fonts_->face(item.bold);
        font.shape(item.text, glyphs_);
        if (glyphs_.empty()) continue;

        advances_.resize(glyphs_.size());
        for (size_t i = 0; i < glyphs_.size(); ++i)
            advances_[i] = static_cast<float>(font.advance(glyphs_[i]) * item.size);

        GlyphRun run;
        run.glyphs = glyphs_.data();
        run.advances = advances_.data();
        run.count = glyphs_.size();
        canvas.drawGlyphs(item.x, item.y, item.size, item.bold, run, item.color);
    }
}

}  // namespace cvb
