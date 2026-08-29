# Cross-compiles the Windows binaries with mingw-w64, which is how the Windows
# build has always been produced here (from WSL or from any Linux box):
#
#   sudo apt install -y g++-mingw-w64-x86-64
#
# Under MSYS2 or any native mingw shell this file is not needed — the host
# toolchain already targets Windows.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CVB_MINGW_PREFIX x86_64-w64-mingw32)

set(CMAKE_C_COMPILER ${CVB_MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${CVB_MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER ${CVB_MINGW_PREFIX}-windres)

set(CMAKE_FIND_ROOT_PATH /usr/${CVB_MINGW_PREFIX})
# Tools are the host's; headers and libraries must come from the target sysroot,
# or a Linux libstdc++ would be found and linked into a Windows executable.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
