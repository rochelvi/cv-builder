#include "console.h"

namespace cvb {
namespace platform {

// argv is already the byte sequence the file system uses, which is what a UTF-8
// std::string means here, so the arguments are passed through untouched. In
// particular they are *not* re-encoded: a name that is not valid UTF-8 is still
// a valid file name on these systems, and mangling it would lose the file.
std::vector<std::string> commandLine(int argc, char** argv) {
    std::vector<std::string> out;
    out.reserve(static_cast<size_t>(argc < 0 ? 0 : argc));
    for (int i = 0; i < argc; ++i) out.emplace_back(argv[i] ? argv[i] : "");
    return out;
}

// Nothing to do: the terminal decides its own encoding and every modern one
// speaks UTF-8.
void prepareConsole() {}

}  // namespace platform
}  // namespace cvb
