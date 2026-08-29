// Does the portable preview actually draw the document?
//
// The interesting part is not that the widget appears - it is that the glyph runs
// reach the screen. The preview hands Qt glyph *ids* from our own TrueType reader
// rather than strings, which is what makes it the export rather than an
// impression of it, and that is exactly the kind of thing that fails silently:
// a wrong font blob or an unset raw font draws nothing at all and looks like an
// empty page.
//
// So the pane is rendered into an image and the pixels are counted. Runs headless
// through Qt's offscreen platform, so it works in CI with no display.
#include <QApplication>
#include <QColor>
#include <QImage>
#include <QSet>

#include <cstdio>
#include <string>

#include "fonts.h"
#include "layout.h"
#include "model.h"
#include "preview_pane.h"

namespace {

int gFailures = 0;

void check(bool ok, const char* what) {
    std::printf("%-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) ++gFailures;
}

cvb::CV sample() {
    cvb::CV cv;
    cv.name = "Даниил Мишин";
    cv.role = "SYSTEM ADMINISTRATOR";
    cv.email = "mail@example.com";
    cv.summary = "Короткий абзац о себе, достаточно длинный, чтобы перенестись.";

    cvb::Job job;
    job.title = "Инженер";
    job.period = "2022 – 2025";
    job.company = "Компания";
    job.bullets = {"Первое достижение", "Второе достижение"};
    cv.jobs.push_back(job);
    return cv;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    cvb::FontSet fonts;
    std::string error;
    if (!cvb::app::loadFonts(fonts, error)) {
        std::printf("skipped: no font available (%s)\n", error.c_str());
        return 0;
    }

    const cvb::Document doc = cvb::layout(sample(), fonts);

    cvb::qtui::PreviewPane pane;
    pane.resize(700, 900);
    pane.setFonts(&fonts);
    pane.setDocument(doc);
    pane.setZoom(100);

    QImage image(pane.size(), QImage::Format_ARGB32);
    // Filled with a colour the document cannot contain, so anything still magenta
    // afterwards was never painted.
    image.fill(QColor(255, 0, 255));
    pane.render(&image);

    // The sheet of the default dark template.
    const QRgb paper = qRgb(0x0d, 0x0f, 0x12);
    long paperPixels = 0;
    long unpainted = 0;
    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (pixel == qRgb(255, 0, 255)) ++unpainted;
            if (pixel == paper) ++paperPixels;
            colours.insert(pixel);
        }
    }

    check(unpainted == 0, "every pixel of the viewport was painted");
    // A4 at 100 % is 595 x 842 points, so the sheet is most of a 700 x 900 view.
    check(paperPixels > 200000, "the sheet is drawn at its A4 size");
    // Text and rules on it: the background alone would be a handful of colours,
    // antialiased glyphs are hundreds.
    check(colours.size() > 50, "glyphs and rules were rasterised onto the sheet");

    // And the ink is where the layout engine put it: the name sits near the top
    // left of the sheet, so that band must contain something other than paper.
    bool inkNearTheName = false;
    for (int y = 20; y < 90 && !inkNearTheName; ++y)
        for (int x = 60; x < 400; ++x)
            if (image.pixel(x, y) != paper && image.pixel(x, y) != qRgb(0x2a, 0x2c, 0x2f)) {
                inkNearTheName = true;
                break;
            }
    check(inkNearTheName, "the name is drawn where the layout engine put it");

    std::printf("\n%s\n", gFailures == 0 ? "preview renders" : "preview does not render");
    return gFailures == 0 ? 0 : 1;
}
