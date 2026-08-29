# The core has no window and no operating system in it, so it can be tested
# without either. Nothing here links a UI target - if it ever needs to, the
# layering has been broken.
add_executable(resume_tests
               harness.cpp
               test_numeric.cpp
               test_file.cpp
               test_font.cpp
               test_model.cpp
               test_layout.cpp
               test_pdf.cpp)
target_link_libraries(resume_tests PRIVATE resume_core resume_render resume_app)

# The font cases load the bundled face the way the program does - from beside the
# executable - so the assets have to be there.
cvb_stage_assets(resume_tests)

add_test(NAME core COMMAND resume_tests)

if(CVB_BUILD_QT_UI)
    add_subdirectory(qt)
endif()
