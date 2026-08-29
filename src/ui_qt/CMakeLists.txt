# The portable front end: one window, three platforms.
#
# Qt Widgets rather than Qt Quick. The editing pane is a dense form of a few
# hundred fields, which is what widgets are for; QML would have added a JavaScript
# engine and a scene graph and given nothing back. Of the toolkit only Core, Gui,
# Widgets and PrintSupport are used - no QML, no Network, no WebEngine.
#
# What matters for the design: this target draws the document through
# resume_render, exactly as the Win32 one does, and reaches the operating system
# only through resume_platform. It holds no layout code and no file format of its
# own, which is why the two front ends cannot disagree about the page.
#
# Split into a library and a thin executable so that the panes can be driven by a
# test as well as by a person - the same split the Win32 front end has.
qt_standard_project_setup()

qt_add_library(resume_ui_qt STATIC
               main_window.cpp
               editor_pane.cpp
               editor_widgets.cpp
               preview_pane.cpp
               theme.cpp)

target_include_directories(resume_ui_qt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_ui_qt PUBLIC
                      resume_core
                      resume_render
                      resume_app
                      resume_platform
                      resume_uicommon
                      resume_version
                      Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)

qt_add_executable(cvbuilder-qt WIN32 MACOSX_BUNDLE main.cpp)
target_link_libraries(cvbuilder-qt PRIVATE resume_ui_qt)

# The Win32 resource carries the icon and the version information; on the other
# platforms Qt and the desktop environment take care of that.
if(WIN32)
    target_sources(cvbuilder-qt PRIVATE "${PROJECT_SOURCE_DIR}/res/app.rc")
endif()

set_target_properties(cvbuilder-qt PROPERTIES
                      OUTPUT_NAME "CVBuilder-Qt"
                      MACOSX_BUNDLE_BUNDLE_NAME "CV Builder"
                      MACOSX_BUNDLE_GUI_IDENTIFIER "com.github.rochelvi.cv-builder"
                      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# The bundled font and the sample have to sit beside the executable, the same as
# for the other runnable targets.
cvb_stage_assets(cvbuilder-qt)
add_custom_command(TARGET cvbuilder-qt POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_if_different
                           "${PROJECT_SOURCE_DIR}/sample_cv.json"
                           "$<TARGET_FILE_DIR:cvbuilder-qt>/sample_cv.json"
                   VERBATIM)
