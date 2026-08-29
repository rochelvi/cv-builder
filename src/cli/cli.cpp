// Console front end: renders a CV JSON to PDF, and can dump the layout for
// comparison against a reference implementation.
//
//   cvcli in.json out.pdf [--dump-ops ops.json] [--fonts regular.ttf bold.ttf]
//   cvcli --font-report [--fonts regular.ttf bold.ttf]
//   cvcli --version
#include <cstdio>
#include <string>
#include <vector>

#include "console.h"
#include "file.h"
#include "font.h"
#include "fonts.h"
#include "layout.h"
#include "model.h"
#include "numeric.h"
#include "pdf.h"
#include "version.h"

namespace {

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

// A flat JSON list of every draw call, so the layout can be diffed line by line
// against the original renderer. Coordinates go through numfmt for the same
// reason the PDF writer does: a comma here would make the dump unparseable in
// half of Europe.
bool dumpOps(const cvb::Document& doc, const cvb::Path& path) {
    auto f3 = [](double v) { return numfmt::fixed(v, 3); };
    char buf[512];
    std::string out = "[\n";
    bool first = true;
    for (size_t p = 0; p < doc.pages.size(); ++p) {
        const cvb::Page& page = doc.pages[p];
        for (const cvb::LineItem& line : page.lines) {
            std::snprintf(buf, sizeof buf, "%s {\"op\":\"rule\",\"page\":%zu,\"y\":%s,\"w\":%s}",
                          first ? "" : ",\n", p, f3(line.y1).c_str(), f3(line.width).c_str());
            out += buf;
            first = false;
        }
        for (const cvb::TextItem& item : page.texts) {
            std::snprintf(buf, sizeof buf,
                          "%s {\"op\":\"text\",\"page\":%zu,\"x\":%s,\"y\":%s,\"size\":%s,"
                          "\"bold\":%s,\"color\":\"%02x%02x%02x\",\"s\":",
                          first ? "" : ",\n", p, f3(item.x).c_str(), f3(item.y).c_str(),
                          numfmt::shortest(item.size).c_str(),
                          item.bold ? "true" : "false", item.color.r, item.color.g, item.color.b);
            // The text goes on separately: a bullet list runs well past any
            // fixed buffer, and truncating the dump would make it lie.
            out += buf;
            out += '"';
            out += escape(item.text);
            out += "\"}";
            first = false;
        }
    }
    out += "\n]\n";

    std::string error;
    return cvb::writeFile(path, out, error);
}

// Every glyph metric the layout engine can depend on, as text.
//
// The page design is a function of advance widths: change them and line breaks
// move, which moves baselines, which can add a page. So "these two faces are
// metric-compatible" is a claim that has to be checkable rather than believed -
// diff two of these reports and you know, for every character the program can
// set, whether the two faces lay out the same.
void fontReport(const cvb::FontSet& fonts) {
    // The ranges the template can actually use: ASCII, Latin-1 for the accented
    // names, Cyrillic, and the four typographic characters the layout engine
    // emits itself (the bullet mark, the middot, the em dash, the ellipsis).
    struct Range { uint32_t first, last; };
    static const Range ranges[] = {
        {0x0020, 0x007E}, {0x00A0, 0x00FF}, {0x0400, 0x04FF},
        {0x2013, 0x2014}, {0x2018, 0x201E}, {0x2022, 0x2022},
        {0x2026, 0x2026}, {0x203A, 0x203A},
    };

    for (int bold = 0; bold < 2; ++bold) {
        const cvb::Font& font = fonts.face(bold != 0);
        std::printf("[%s] family=%s unitsPerEm=%d glyphs=%d ascent=%d descent=%d capHeight=%d\n",
                    bold ? "bold" : "regular", fonts.family().c_str(), font.unitsPerEm(),
                    font.numGlyphs(), font.ascent(), font.descent(), font.capHeight());

        for (const Range& range : ranges) {
            for (uint32_t cp = range.first; cp <= range.last; ++cp) {
                const uint16_t gid = font.glyphFor(cp);
                // The advance is printed per 1000 em, not in font units: two
                // faces may disagree about unitsPerEm and still set text
                // identically, and it is the em fraction the layout uses.
                std::printf("U+%04X %s %s\n", cp, gid ? "present" : "MISSING",
                            numfmt::fixed(font.advance(gid) * 1000.0, 3).c_str());
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    // Asked for again through the platform, because on Windows main's own argv
    // is in a code page that cannot spell every path.
    const std::vector<std::string> args = cvb::platform::commandLine(argc, argv);
    cvb::platform::prepareConsole();
    const size_t count = args.size();

    // Accepted anywhere on the line and answered before anything is read
    // from disk: the console front end has no window to carry the version
    // the way the GUI does, and "which build produced this PDF?" is what a
    // script or a bug report needs answered. Same string the window shows.
    for (size_t i = 1; i < count; ++i) {
        if (args[i] == "--version") {
            std::printf("%s %s\n%s\n", VER_PRODUCT, VER_DISPLAY_STR, VER_COPYRIGHT);
            return 0;
        }
    }

    // --fonts names the pair explicitly, which is how output from one machine
    // can be reproduced on another and how the candidates for the bundled face
    // were compared against each other.
    cvb::FontChoice explicitFonts;
    bool wantReport = false;
    for (size_t i = 1; i < count; ++i) {
        if (args[i] == "--font-report") wantReport = true;
        if (args[i] != "--fonts" || i + 2 >= count) continue;
        explicitFonts.regular = cvb::fromUtf8(args[i + 1]);
        explicitFonts.bold = cvb::fromUtf8(args[i + 2]);
        explicitFonts.family = "explicit";
    }

    if (!wantReport && count < 3) {
        std::fprintf(stderr,
                     "usage: cvcli in.json out.pdf [--dump-ops ops.json]\n"
                     "                            [--fonts regular.ttf bold.ttf]\n"
                     "       cvcli --font-report [--fonts regular.ttf bold.ttf]\n"
                     "       cvcli --version\n");
        return 2;
    }

    std::string error;
    cvb::FontSet fonts;
    const bool loaded = explicitFonts.regular.empty()
                            ? cvb::app::loadFonts(fonts, error)
                            : fonts.load({explicitFonts}, error);
    if (!loaded) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    if (wantReport) {
        fontReport(fonts);
        return 0;
    }

    const cvb::Path input = cvb::fromUtf8(args[1]);
    const cvb::Path output = cvb::fromUtf8(args[2]);

    cvb::CV cv;
    if (!cvb::load(input, cv, error)) {
        std::fprintf(stderr, "не удалось загрузить %s: %s\n", args[1].c_str(), error.c_str());
        return 1;
    }

    cvb::Document doc = cvb::layout(cv, fonts);
    if (!cvb::writePdf(doc, fonts, output, error)) {
        std::fprintf(stderr, "не удалось записать PDF: %s\n", error.c_str());
        return 1;
    }

    for (size_t i = 3; i + 1 < count; ++i)
        if (args[i] == "--dump-ops") dumpOps(doc, cvb::fromUtf8(args[i + 1]));

    std::printf("%s: %zu страниц(ы)\n", args[2].c_str(), doc.pages.size());
    return 0;
}
