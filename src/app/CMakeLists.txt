# Application services that are the same everywhere: what the recent-files list
# does, when a recovery snapshot is worth offering. They sit above the platform
# layer and below any front end, so both front ends share one behaviour rather
# than each inventing its own.
add_library(resume_app STATIC
            recent_files.cpp
            recovery.cpp)

target_include_directories(resume_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(resume_app PUBLIC resume_core resume_platform)
