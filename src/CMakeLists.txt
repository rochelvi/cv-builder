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

# ------------------------------------------------------------------- core
# The resume itself: JSON, the model, the layout engine, the TrueType reader and
# the PDF writer. This is the part that has to stay free of any operating
# system, and the one the tests can exercise without a window.
add_library(resume_core STATIC
            numeric.cpp
            json.cpp
            model.cpp
            font.cpp
            layout.cpp
            pdf.cpp)
target_include_directories(resume_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_core PUBLIC resume_options)

# ------------------------------------------------------------- Win32 front end
# The original implementation, kept as the reference the portable front end is
# measured against. Frozen: new work goes to the cross-platform UI.
if(CVB_BUILD_WIN32_UI)
    if(NOT WIN32)
        message(FATAL_ERROR "CVB_BUILD_WIN32_UI needs a Windows target")
    endif()

    add_library(resume_ui_win32 STATIC
                form.cpp
                preview.cpp
                theme.cpp
                settings.cpp
                print.cpp)
    # PUBLIC: a static library does not link, so the executable that consumes it
    # is the one that needs these.
    target_link_libraries(resume_ui_win32 PUBLIC
                          resume_core
                          comctl32 comdlg32 d2d1 dwrite dwmapi uxtheme
                          gdi32 user32 shlwapi advapi32 ole32 uuid shell32)

    add_executable(CVBuilder WIN32 app.cpp "${PROJECT_SOURCE_DIR}/res/app.rc")
    target_link_libraries(CVBuilder PRIVATE resume_ui_win32 resume_version)

    # The app reads sample_cv.json from its own directory at startup and never
    # writes to it, so a copy next to the executable is all it needs.
    add_custom_command(TARGET CVBuilder POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different
                               "${PROJECT_SOURCE_DIR}/sample_cv.json"
                               "$<TARGET_FILE_DIR:CVBuilder>/sample_cv.json"
                       VERBATIM)
endif()

# -------------------------------------------------------------------- cvcli
# Renders a CV to PDF with no window in sight, which also makes it the tool the
# regression tests drive.
add_executable(cvcli cli.cpp)
target_link_libraries(cvcli PRIVATE resume_core resume_version)
if(WIN32)
    target_sources(cvcli PRIVATE "${PROJECT_SOURCE_DIR}/res/cli.rc")
    target_link_libraries(cvcli PRIVATE shell32)
endif()
