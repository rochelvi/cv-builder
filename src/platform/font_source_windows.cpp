#include "font_source.h"

#include <windows.h>

#include <string>

namespace cvb {
namespace platform {

std::vector<FontChoice> systemFontCandidates() {
    wchar_t windir[MAX_PATH] = {0};
    const UINT n = GetWindowsDirectoryW(windir, MAX_PATH);
    const Path fonts = Path(n ? std::wstring(windir, n) : std::wstring(L"C:\\Windows")) / L"Fonts";

    // Arial first: it is metrically identical to the template's Helvetica, so
    // line breaks land exactly where the original design put them. The rest are
    // here so the program still runs on a system stripped of it, not because
    // they lay out the same way - they do not.
    struct Face {
        const wchar_t* regular;
        const wchar_t* bold;
        const char* family;  // ASCII, so already valid UTF-8
    };
    static const Face faces[] = {
        {L"arial.ttf", L"arialbd.ttf", "Arial"},
        {L"segoeui.ttf", L"segoeuib.ttf", "Segoe UI"},
        {L"tahoma.ttf", L"tahomabd.ttf", "Tahoma"},
        {L"verdana.ttf", L"verdanab.ttf", "Verdana"},
        {L"calibri.ttf", L"calibrib.ttf", "Calibri"},
    };

    std::vector<FontChoice> out;
    for (const Face& face : faces) {
        FontChoice choice;
        choice.regular = fonts / face.regular;
        choice.bold = fonts / face.bold;
        choice.family = face.family;
        out.push_back(std::move(choice));
    }
    return out;
}

}  // namespace platform
}  // namespace cvb
