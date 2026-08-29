# The resume itself: JSON, the model, the layout engine, the TrueType reader and
# the PDF writer.
#
# This target must stay free of any operating system. It links no platform
# library and includes no platform header, which is what lets the tests exercise
# it without a window and lets the same object code serve every front end.
add_library(resume_core STATIC
            numeric.cpp
            json.cpp
            model.cpp
            font.cpp
            layout.cpp
            pdf.cpp)

target_include_directories(resume_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_core PUBLIC resume_options)
