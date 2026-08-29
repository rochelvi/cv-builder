# A smoke test for the portable preview: that the document really is rasterised,
# glyph by glyph, and not merely that the widget exists. Runs through Qt's
# offscreen platform, so it needs no display.
#
# Its own executable rather than a case in resume_tests, which must stay free of
# any toolkit so that the core can be tested where none is installed.
qt_add_executable(resume_qt_tests test_preview_render.cpp)

target_link_libraries(resume_qt_tests PRIVATE resume_ui_qt)
cvb_stage_assets(resume_qt_tests)

add_test(NAME preview COMMAND resume_qt_tests)
# Offscreen: no window server, no display, no user watching.
set_tests_properties(preview PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Renders the whole window to a PNG with no display. Not a test - a way to see
# what the interface looks like on a machine that cannot show it, and to attach a
# picture to a bug report.
qt_add_executable(resume_qt_shot screenshot.cpp)
target_link_libraries(resume_qt_shot PRIVATE resume_ui_qt)
cvb_stage_assets(resume_qt_shot)
add_custom_command(TARGET resume_qt_shot POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                           "${PROJECT_SOURCE_DIR}/sample_cv.json"
                           "$<TARGET_FILE_DIR:resume_qt_shot>/sample_cv.json"
                   VERBATIM)
