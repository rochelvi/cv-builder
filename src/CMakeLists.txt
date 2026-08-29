# ---------------------------------------------------------------- options
# Compiler settings travel with the targets that need them instead of being set
# globally: a target that links resume_options gets C++17 and the warning set,
# and a target that does not is unaffected.
add_library(resume_options INTERFACE)
target_compile_features(resume_options INTERFACE cxx_std_17)

if(MSVC)
    # /utf-8: the sources hold Russian string literals and are UTF-8 without a
    # BOM, which cl.exe otherwise reads in the system code page.
    target_compile_options(resume_options INTERFACE /W4 /permissive- /utf-8)
else()
    target_compile_options(resume_options INTERFACE -Wall -Wextra)
endif()

if(WIN32)
    target_compile_definitions(resume_options INTERFACE
                               UNICODE _UNICODE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
endif()

if(MINGW)
    # Matches what the hand-written Makefile produced: an .exe that runs on a
    # machine with no runtime installed, with unused sections dropped.
    target_compile_options(resume_options INTERFACE -ffunction-sections -fdata-sections)
    target_link_options(resume_options INTERFACE
                        -static -static-libgcc -static-libstdc++ -Wl,--gc-sections)
endif()

# res/version.h, wanted by whoever prints or embeds the version.
add_library(resume_version INTERFACE)
target_include_directories(resume_version INTERFACE "${PROJECT_SOURCE_DIR}/res")

add_subdirectory(core)
add_subdirectory(cli)

if(CVB_BUILD_WIN32_UI)
    add_subdirectory(ui_win32)
endif()
