#include "fonts.h"

#include "font_source.h"
#include "paths.h"

namespace cvb {
namespace app {
namespace {

// The bundled pair, by file name rather than by family: this is a file the
// project carries, not something the system is asked about. Liberation Sans,
// whose advance widths were measured to match Arial's - the metrics the template
// was designed around - for every code point the program can set. See
// assets/fonts/README.md for the measurement.
const char* const kBundledRegular = "LiberationSans-Regular.ttf";
const char* const kBundledBold = "LiberationSans-Bold.ttf";
const char* const kBundledFamily = "Liberation Sans";

}  // namespace

std::vector<Path> bundledFontRoots() {
    std::vector<Path> roots;
    const Path exe = platform::executableDirectory();
    if (!exe.empty()) {
        roots.push_back(exe / "assets" / "fonts");  // installed, and portable copies
        roots.push_back(exe / "fonts");
        roots.push_back(exe);
        // An installed Unix layout puts the binary in bin/ and its data in
        // share/<name>/, so the assets are a sibling of the directory above.
        roots.push_back(exe.parent_path() / "share" / "cv-builder" / "fonts");
        // Run straight out of a build directory, where the assets are still in
        // the source tree a couple of levels up.
        roots.push_back(exe.parent_path().parent_path() / "assets" / "fonts");
        roots.push_back(exe.parent_path().parent_path().parent_path() / "assets" / "fonts");
    }
    return roots;
}

bool loadFonts(FontSet& out, std::string& error) {
    std::vector<FontChoice> candidates;

    for (const Path& root : bundledFontRoots()) {
        FontChoice choice;
        choice.regular = root / kBundledRegular;
        choice.bold = root / kBundledBold;
        choice.family = kBundledFamily;
        std::error_code ec;
        // Checked here rather than left to the loader so a missing bundle does
        // not fill the error message with one failure per candidate directory.
        if (!std::filesystem::is_regular_file(choice.regular, ec)) continue;
        candidates.push_back(std::move(choice));
    }

    const std::vector<FontChoice> system = platform::systemFontCandidates();
    candidates.insert(candidates.end(), system.begin(), system.end());

    return out.load(candidates, error);
}

bool usingBundledFonts(const FontSet& fonts) {
    // By path, not by family name: the same face is also a system font on most
    // Linux distributions, and "the copy we shipped" is a different statement
    // from "a font with that name". The metrics happen to be the same either
    // way, which is exactly why the two must not be confused.
    const Path& regular = fonts.regular().path();
    if (regular.empty() || regular.filename() != Path(kBundledRegular)) return false;
    for (const Path& root : bundledFontRoots())
        if (platform::samePath(root / kBundledRegular, regular)) return true;
    return false;
}

}  // namespace app
}  // namespace cvb
