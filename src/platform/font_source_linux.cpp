#include "font_source.h"

#include <cstdlib>
#include <string>

namespace cvb {
namespace platform {
namespace {

// The usual places, most specific first, so a face the user installed for
// themselves wins over the distribution's copy of the same name. Deliberately
// not fontconfig: it would be the program's first external dependency, it
// answers "a sans-serif face" rather than "this face", and the file it hands
// back may be an OpenType/CFF or a variable font that this TrueType reader
// cannot use anyway.
std::vector<Path> fontRoots() {
    std::vector<Path> roots;
    if (const char* home = std::getenv("HOME")) {
        const char* dataHome = std::getenv("XDG_DATA_HOME");
        if (dataHome && dataHome[0] == '/')
            roots.push_back(Path(dataHome) / "fonts");
        else
            roots.push_back(Path(home) / ".local" / "share" / "fonts");
        roots.push_back(Path(home) / ".fonts");
    }
    roots.push_back("/usr/local/share/fonts");
    roots.push_back("/usr/share/fonts");
    return roots;
}

// Fonts are filed differently by every packager - truetype/liberation-sans,
// truetype/liberation, liberation-sans, or straight into the root - so the file
// is looked for by name rather than by an assumed path.
Path findByName(const std::vector<Path>& roots, const char* name) {
    std::error_code ec;
    for (const Path& root : roots) {
        if (!std::filesystem::is_directory(root, ec)) continue;
        // Follows symlinks and skips anything unreadable rather than throwing:
        // /usr/share/fonts is full of links, and a broken one is not our problem.
        auto options = std::filesystem::directory_options::follow_directory_symlink |
                       std::filesystem::directory_options::skip_permission_denied;
        for (std::filesystem::recursive_directory_iterator it(root, options, ec), end;
             it != end; it.increment(ec)) {
            if (ec) break;
            if (it->path().filename() == name) return it->path();
        }
    }
    return Path();
}

}  // namespace

std::vector<FontChoice> systemFontCandidates() {
    const std::vector<Path> roots = fontRoots();

    // Liberation Sans and Arimo are metric-compatible with Arial by design, so
    // they hold the template's line breaks; DejaVu and Noto are wider and will
    // wrap differently. That is why they come last, and why the bundled font -
    // tried before this list - exists at all.
    struct Face {
        const char* regular;
        const char* bold;
        const char* family;
    };
    static const Face faces[] = {
        {"LiberationSans-Regular.ttf", "LiberationSans-Bold.ttf", "Liberation Sans"},
        {"Arimo-Regular.ttf", "Arimo-Bold.ttf", "Arimo"},
        {"Arial.ttf", "Arial_Bold.ttf", "Arial"},
        {"arial.ttf", "arialbd.ttf", "Arial"},
        {"DejaVuSans.ttf", "DejaVuSans-Bold.ttf", "DejaVu Sans"},
        {"NotoSans-Regular.ttf", "NotoSans-Bold.ttf", "Noto Sans"},
        {"FreeSans.ttf", "FreeSansBold.ttf", "FreeSans"},
        {"Ubuntu-R.ttf", "Ubuntu-B.ttf", "Ubuntu"},
    };

    std::vector<FontChoice> out;
    for (const Face& face : faces) {
        FontChoice choice;
        choice.regular = findByName(roots, face.regular);
        if (choice.regular.empty()) continue;
        choice.bold = findByName(roots, face.bold);
        if (choice.bold.empty()) continue;
        choice.family = face.family;
        out.push_back(std::move(choice));
    }
    return out;
}

}  // namespace platform
}  // namespace cvb
