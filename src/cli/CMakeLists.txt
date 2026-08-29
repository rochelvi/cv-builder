# Renders a CV to PDF with no window in sight, which also makes it the tool the
# regression tests drive.
add_executable(cvcli cli.cpp)
target_link_libraries(cvcli PRIVATE resume_core resume_version)

if(WIN32)
    target_sources(cvcli PRIVATE "${PROJECT_SOURCE_DIR}/res/cli.rc")
    target_link_libraries(cvcli PRIVATE shell32)
endif()
