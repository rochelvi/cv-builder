// Console front end: renders a CV JSON to PDF, and can dump the layout for
// comparison against a reference implementation.
//
//   cvcli in.json out.pdf [--dump-ops ops.json]
//   cvcli --version
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <string>

#include "file.h"
#include "font.h"
#include "layout.h"
#include "model.h"
#include "numeric.h"
#include "pdf.h"
#include "version.h"

namespace {

std::string narrow(const std::wstring& text) {
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), n, nullptr, nullptr);
    return out;
}

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

}  // namespace

int main() {
    // Wide arguments, so paths with non-ASCII characters survive.
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 2;
    SetConsoleOutputCP(CP_UTF8);

    // Accepted anywhere on the line and answered before anything is read
    // from disk: the console front end has no window to carry the version
    // the way the GUI does, and "which build produced this PDF?" is what a
    // script or a bug report needs answered. Same string the window shows.
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--version") {
            std::printf("%s %s\n%s\n", VER_PRODUCT, VER_DISPLAY_STR, VER_COPYRIGHT);
            return 0;
        }
    }

    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: cvcli in.json out.pdf [--dump-ops ops.json]\n"
                     "       cvcli --version\n");
        return 2;
    }

    std::string error;
    cvb::CV cv;
    if (!cvb::load(argv[1], cv, error)) {
        std::fprintf(stderr, "не удалось загрузить %s: %s\n", narrow(argv[1]).c_str(), error.c_str());
        return 1;
    }

    cvb::FontSet fonts;
    if (!fonts.loadSystem(error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    cvb::Document doc = cvb::layout(cv, fonts);
    if (!cvb::writePdf(doc, fonts, argv[2], error)) {
        std::fprintf(stderr, "не удалось записать PDF: %s\n", error.c_str());
        return 1;
    }

    for (int i = 3; i + 1 < argc; ++i)
        if (std::wstring(argv[i]) == L"--dump-ops") dumpOps(doc, argv[i + 1]);

    std::printf("%s: %zu страниц(ы)\n", narrow(argv[2]).c_str(), doc.pages.size());
    return 0;
}
