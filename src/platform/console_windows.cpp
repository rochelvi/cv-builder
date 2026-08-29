#include "console.h"

#include <windows.h>

#include <shellapi.h>

#include "text_windows.h"

namespace cvb {
namespace platform {

std::vector<std::string> commandLine(int, char**) {
    // main's argv is in the process code page, which cannot represent a path
    // outside it; the wide command line can, so the arguments are taken from
    // there and re-encoded as UTF-8.
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return {};

    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) out.push_back(utf8FromWide(wide[i]));
    LocalFree(wide);
    return out;
}

void prepareConsole() { SetConsoleOutputCP(CP_UTF8); }

}  // namespace platform
}  // namespace cvb
