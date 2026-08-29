// The bundled face, pinned.
//
// Advance widths are not a detail of the font: the layout engine breaks lines by
// them, so a different set of widths moves every baseline below the first change
// and can add a page. That makes the numbers below part of the document format,
// not an implementation detail - if replacing the font file changes one of them,
// every resume already written lays out differently, and that has to be a
// deliberate decision rather than a surprise.
//
// The values are Arial's, which is what the template was designed around and
// what the Windows-only version always used. Liberation Sans reproduces them
// exactly; see assets/fonts/README.md.
#include <cstdint>
#include <string>
#include <vector>

#include "font.h"
#include "fonts.h"
#include "harness.h"
#include "layout.h"
#include "model.h"

namespace {

// The bundled pair, or an unloaded set when the assets are not beside the test
// binary - in which case the cases below skip rather than testing the system
// font and pretending it proves something.
const cvb::FontSet* bundled() {
    static cvb::FontSet fonts;
    static bool tried = false;
    static bool ok = false;
    if (!tried) {
        tried = true;
        std::string error;
        ok = cvb::app::loadFonts(fonts, error) && cvb::app::usingBundledFonts(fonts);
    }
    return ok ? &fonts : nullptr;
}

// Rounded to whole thousandths of an em, which is the precision the PDF writer
// emits and enough to catch any real change of metrics.
long advanceMilliEm(const cvb::Font& font, uint32_t codePoint) {
    const uint16_t gid = font.glyphFor(codePoint);
    return static_cast<long>(font.advance(gid) * 1000.0 + 0.5);
}

}  // namespace

TEST(font_bundled_is_the_one_in_use) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    CHECK(fonts->valid());
    CHECK_EQ(fonts->family(), std::string("Liberation Sans"));
    // Regular and bold must be two different files, or bold text would be set
    // with the regular metrics and the layout would be subtly wrong.
    CHECK(fonts->regular().path() != fonts->bold().path());
}

TEST(font_advances_match_the_template_metrics) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }

    struct Expected {
        uint32_t codePoint;
        long regular;
        long bold;
    };
    // Arial's advances, per 1000 em: the characters that carry the most text in
    // a resume, plus the ones the layout engine emits itself. Measured from
    // Arial rather than recalled - tools/metricstable.sh prints this table from
    // any font, which is how it was produced and how it can be checked again.
    static const Expected expected[] = {
        {0x0020, 278, 278},    // space
        {0x0041, 667, 722},    // A
        {0x004D, 833, 833},    // M
        {0x0061, 556, 556},    // a
        {0x0069, 222, 278},    // i
        {0x006C, 222, 278},    // l
        {0x006D, 833, 889},    // m
        {0x0077, 722, 778},    // w
        {0x0030, 556, 556},    // 0
        {0x002D, 333, 333},    // -
        {0x002E, 278, 278},    // .
        {0x002C, 278, 278},    // ,
        {0x002F, 278, 278},    // /
        {0x0040, 1015, 975},   // @, in every email address
        {0x0410, 667, 722},    // А
        {0x0430, 556, 556},    // а
        {0x044F, 542, 583},    // я
        {0x0451, 556, 556},    // ё
        {0x00B7, 333, 333},    // middot, the skill and contact separator
        {0x203A, 333, 333},    // the bullet mark
        {0x2014, 1000, 1000},  // em dash, in the window title and the PDF title
        {0x2026, 1000, 1000},  // ellipsis
    };

    for (const Expected& item : expected) {
        CHECK_EQ(advanceMilliEm(fonts->regular(), item.codePoint), item.regular);
        CHECK_EQ(advanceMilliEm(fonts->bold(), item.codePoint), item.bold);
    }
}

TEST(font_covers_what_the_template_can_set) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }

    struct Range { uint32_t first, last; };
    static const Range ranges[] = {
        {0x0020, 0x007E},  // ASCII
        {0x00C0, 0x00FF},  // accented Latin, for names
        {0x0410, 0x044F},  // Cyrillic
    };
    for (int bold = 0; bold < 2; ++bold) {
        const cvb::Font& font = fonts->face(bold != 0);
        for (const Range& range : ranges)
            for (uint32_t cp = range.first; cp <= range.last; ++cp)
                if (!CHECK(font.glyphFor(cp) != 0)) return;  // one report is enough
    }
}

TEST(font_measures_a_string_by_summing_advances) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }
    const cvb::Font& font = fonts->regular();

    // Nine points is the body size the template uses.
    const double space = font.advance(font.glyphFor(U' ')) * 9.0;
    const double a = font.advance(font.glyphFor(U'a')) * 9.0;
    CHECK(std::abs(font.measure("a a", 9.0) - (a + space + a)) < 1e-9);

    // UTF-8 is decoded rather than counted in bytes: two Cyrillic letters are
    // four bytes and must measure as two glyphs.
    const double cyrillic = font.advance(font.glyphFor(0x0430)) * 9.0;
    CHECK(std::abs(font.measure("аа", 9.0) - 2 * cyrillic) < 1e-9);

    CHECK_EQ(font.measure("", 9.0), 0.0);
}

TEST(font_reads_a_face_out_of_a_collection) {
    const cvb::FontSet* fonts = bundled();
    if (!fonts) {
        test::skip("bundled font not beside the test binary");
        return;
    }

    // macOS ships most of its faces as .ttc collections, so the reader has to
    // handle them - but no collection is guaranteed to be installed on the
    // machine running the tests. One is built here instead: a ttcf header whose
    // single entry points at the table directory of a real font, which is
    // exactly what a collection of one face is.
    std::string bytes;
    std::string error;
    CHECK(cvb::readFile(fonts->regular().path(), bytes, error));
    if (bytes.empty()) return;

    const uint32_t offset = 16;  // header: tag, version, count, one offset
    std::vector<uint8_t> collection;
    const uint8_t header[] = {
        't', 't', 'c', 'f',
        0x00, 0x01, 0x00, 0x00,                      // version 1.0
        0x00, 0x00, 0x00, 0x01,                      // one face
        0x00, 0x00, 0x00, static_cast<uint8_t>(offset),
    };
    collection.insert(collection.end(), header, header + sizeof header);
    collection.insert(collection.end(), bytes.begin(), bytes.end());

    // Table offsets inside a collection are measured from the start of the file,
    // so shifting the sfnt down by the header means every one of them has to
    // move with it. This is the same fix-up a real collection has baked in.
    auto put32 = [&](size_t at, uint32_t value) {
        collection[at] = static_cast<uint8_t>(value >> 24);
        collection[at + 1] = static_cast<uint8_t>(value >> 16);
        collection[at + 2] = static_cast<uint8_t>(value >> 8);
        collection[at + 3] = static_cast<uint8_t>(value);
    };
    auto get32 = [&](size_t at) {
        return (static_cast<uint32_t>(collection[at]) << 24) |
               (static_cast<uint32_t>(collection[at + 1]) << 16) |
               (static_cast<uint32_t>(collection[at + 2]) << 8) |
               static_cast<uint32_t>(collection[at + 3]);
    };
    const uint32_t tables = (static_cast<uint32_t>(collection[offset + 4]) << 8) |
                            static_cast<uint32_t>(collection[offset + 5]);
    for (uint32_t i = 0; i < tables; ++i) {
        const size_t record = offset + 12 + 16 * static_cast<size_t>(i);
        put32(record + 8, get32(record + 8) + offset);
    }

    cvb::Font face;
    CHECK(face.loadFromMemory(collection, 0, error));
    CHECK_EQ(face.faceIndex(), 0);
    CHECK_EQ(face.unitsPerEm(), fonts->regular().unitsPerEm());
    CHECK_EQ(face.numGlyphs(), fonts->regular().numGlyphs());
    CHECK_EQ(advanceMilliEm(face, U'A'), advanceMilliEm(fonts->regular(), U'A'));

    // A collection of one face has no face 1, and asking for it is an error
    // rather than silently reading the wrong thing.
    cvb::Font missing;
    CHECK(!missing.loadFromMemory(collection, 1, error));
}
