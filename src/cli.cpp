// Console front end: renders a CV JSON to PDF, and can dump the layout for
// comparison against a reference implementation.
//
//   cvcli in.json out.pdf [--dump-ops ops.json]
//   cvcli --version
#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>  // _wfopen: Unicode paths
#include <string>

#include "font.h"
#include "layout.h"
#include "model.h"
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
// against the original renderer.
bool dumpOps(const cvb::Document& doc, const std::wstring& path) {
    FILE* fh = _wfopen(path.c_str(), L"wb");
    if (!fh) return false;
    std::fprintf(fh, "[\n");
    bool first = true;
    for (size_t p = 0; p < doc.pages.size(); ++p) {
        const cvb::Page& page = doc.pages[p];
        for (const cvb::LineItem& line : page.lines) {
            std::fprintf(fh, "%s {\"op\":\"rule\",\"page\":%zu,\"y\":%.3f,\"w\":%.3f}",
                         first ? "" : ",\n", p, line.y1, line.width);
            first = false;
        }
        for (const cvb::TextItem& item : page.texts) {
            std::fprintf(fh,
                         "%s {\"op\":\"text\",\"page\":%zu,\"x\":%.3f,\"y\":%.3f,\"size\":%g,"
                         "\"bold\":%s,\"color\":\"%02x%02x%02x\",\"s\":\"%s\"}",
                         first ? "" : ",\n", p, item.x, item.y, item.size,
                         item.bold ? "true" : "false", item.color.r, item.color.g, item.color.b,
                         escape(item.text).c_str());
            first = false;
        }
    }
    std::fprintf(fh, "\n]\n");
    std::fclose(fh);
    return true;
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
