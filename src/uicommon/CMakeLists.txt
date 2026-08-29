# What both front ends need and neither owns: the words the editor puts on the
# model's concepts, and the palette the interface is painted in.
#
# Deliberately not part of resume_core, which has no business holding interface
# text or colours, and deliberately not inside either front end, because then the
# two would drift and the application would stop looking like one program.
add_library(resume_uicommon STATIC
            text.cpp
            palette.cpp)

target_include_directories(resume_uicommon PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_uicommon PUBLIC resume_core)
