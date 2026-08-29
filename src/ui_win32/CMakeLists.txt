# The original Win32 / Direct2D front end, kept as the reference the portable
# one is measured against - pixel for pixel through the layout dump, byte for
# byte through the PDF. Frozen: new work goes to the cross-platform UI.
if(NOT WIN32)
    message(FATAL_ERROR "CVB_BUILD_WIN32_UI needs a Windows target")
endif()

add_library(resume_ui_win32 STATIC
            form.cpp
            preview.cpp
            theme.cpp
            settings.cpp
            print.cpp)

target_include_directories(resume_ui_win32 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# PUBLIC: a static library does not link, so the executable that consumes it is
# the one that needs these.
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
