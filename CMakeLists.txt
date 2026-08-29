# CV Builder — one build description for Windows, Linux and macOS.
#
# Targets are split along the layers of the program, so a link error is the
# build system telling you an architectural rule was broken:
#
#   resume_core        model, layout, TrueType, PDF, JSON — no OS, no UI
#   resume_ui_win32    the original Win32 / Direct2D front end (Windows only)
#   CVBuilder          the desktop application
#   cvcli              the console renderer
#
# Nothing here selects sources with #ifdef: platform files are chosen by the
# build, which is what keeps the C++ free of them.
#
#   cmake --preset linux          && cmake --build build/linux
#   cmake --preset windows-mingw  && cmake --build build/windows-mingw
#   cmake --preset windows-msvc   && cmake --build build/windows-msvc
cmake_minimum_required(VERSION 3.21)

# The version lives in res/version.h and nowhere else — the .rc files include
# it and the installer reads it back out of the built .exe. Parsing it here
# rather than repeating it keeps that promise intact.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/res/version.h" CVB_VERSION_HEADER)
foreach(part MAJOR MINOR PATCH)
    string(REGEX MATCH "#define VER_${part} +([0-9]+)" _ "${CVB_VERSION_HEADER}")
    set(CVB_VER_${part} "${CMAKE_MATCH_1}")
    if(NOT CVB_VER_${part} MATCHES "^[0-9]+$")
        message(FATAL_ERROR "res/version.h: VER_${part} not found")
    endif()
endforeach()

project(cv-builder
        VERSION ${CVB_VER_MAJOR}.${CVB_VER_MINOR}.${CVB_VER_PATCH}
        DESCRIPTION "Resume editor with live preview and A4 PDF export"
        LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
    # The .rc files are UTF-8 and hold Russian descriptions. Without being told
    # so, both resource compilers read them in a single-byte code page and the
    # strings come out as mojibake. There is no per-target equivalent of this
    # setting, which is why it sits here rather than on a target.
    if(MINGW)
        string(APPEND CMAKE_RC_FLAGS " --codepage=65001")
    elseif(MSVC)
        string(APPEND CMAKE_RC_FLAGS " /c65001 /nologo")
    endif()
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Only meaningful on Windows; kept as an option so the cross-platform front end
# can be built there too without dragging the Win32 one along.
option(CVB_BUILD_WIN32_UI "Build the original Win32/Direct2D front end" ${WIN32})
option(CVB_BUILD_TESTS "Build the core test suite" ON)

add_subdirectory(src)

if(CVB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
