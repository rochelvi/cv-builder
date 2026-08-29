// The two things a console program cannot do portably: get its arguments and be
// able to print.
//
// On the Unixes both are free - argv is already the bytes the file system uses,
// and the terminal is already UTF-8. On Windows the narrow argv is in a code page
// that cannot spell most paths, so the arguments have to be fetched again from
// the wide command line, and the console has to be told what encoding is coming.
#pragma once

#include <string>
#include <vector>

namespace cvb {
namespace platform {

// The arguments as UTF-8, program name first, whatever the platform hands to
// main. Pass main's own argc and argv; a platform that ignores them will.
std::vector<std::string> commandLine(int argc, char** argv);

// Makes stdout and stderr show UTF-8 correctly. Call once, before printing.
void prepareConsole();

}  // namespace platform
}  // namespace cvb
