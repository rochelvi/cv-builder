#include "font_source.h"

#include <cstdlib>
#include <string>

namespace cvb {
namespace platform {
namespace {

std::vector<Path> fontRoots() {
    std::vector<Path> roots;
    if (const char* home = std::getenv("HOME")) roots.push_back(Path(home) / "Library" / "Fonts");
    roots.push_back("/Library/Fonts");
    // Where the faces that used to sit in /System/Library/Fonts moved to, and
    // where Arial, Verdana and Tahoma live on a current macOS.
    roots.push_back("/System/Library/Fonts/Supplemental");
    roots.push_back("/System/Library/Fonts");
    return roots;
}

Path find(const std::vector<Path>& roots, const char* name) {
    std::error_code ec;
    for (const Path& root : roots) {
        Path candidate = root / name;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }
    return Path();
}

}  // namespace

std::vector<FontChoice> systemFontCandidates() {
    const std::vector<Path> roots = fontRoots();

    // Arial is present on a stock macOS, in Supplemental, and is the same
    // metrics the template was designed for. Helvetica is the historical first
    // choice but ships as a .dfont or an OpenType/CFF collection depending on
    // the release, and this reader needs glyf outlines - so it is not listed:
    // the face has to be one we can subset into the PDF, not merely one we can
    // ask the system to draw.
    struct Face {
        const char* regular;
        const char* bold;
        const char* family;
        int regularIndex;
        int boldIndex;
    };
    static const Face faces[] = {
        {"Arial.ttf", "Arial Bold.ttf", "Arial", 0, 0},
        {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", "Liberation Sans", 0, 0},
        {"Arimo-Regular.ttf", "Arimo-Bold.ttf", "Arimo", 0, 0},
        {"Verdana.ttf", "Verdana Bold.ttf", "Verdana", 0, 0},
        {"Tahoma.ttf", "Tahoma Bold.ttf", "Tahoma", 0, 0},
        // A collection: regular and bold are two faces of one file, which is
        // what the .ttc support in the font reader is for.
        {"Helvetica.ttc", "Helvetica.ttc", "Helvetica", 0, 1},
    };

    std::vector<FontChoice> out;
    for (const Face& face : faces) {
        FontChoice choice;
        choice.regular = find(roots, face.regular);
        if (choice.regular.empty()) continue;
        choice.bold = find(roots, face.bold);
        if (choice.bold.empty()) continue;
        choice.regularFace = face.regularIndex;
        choice.boldFace = face.boldIndex;
        choice.family = face.family;
        out.push_back(std::move(choice));
    }
    return out;
}

}  // namespace platform
}  // namespace cvb
