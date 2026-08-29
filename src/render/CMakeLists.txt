# The one traversal of a laid-out page, shared by every backend that draws
# pixels. Knows about the document and about glyphs; knows nothing about a
# window, a printer or an operating system - which is why it links only the core.
add_library(resume_render STATIC document_painter.cpp)

target_include_directories(resume_render PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_render PUBLIC resume_core)
