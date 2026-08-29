# Everything the program cannot answer without asking the operating system:
# where its own data goes, what the user's documents directory is, where small
# preferences live.
#
# One header per question, one implementation per platform, and the choice made
# here rather than with #ifdef. Adding a platform means adding files to this
# list, not editing the code that calls them.
add_library(resume_platform STATIC paths.h settings.h)

if(WIN32)
    target_sources(resume_platform PRIVATE
                   paths_windows.cpp
                   settings_windows.cpp)
    # shlobj for the known folders, advapi32 for the registry, ole32 for the
    # CoTaskMemFree that SHGetKnownFolderPath's result needs.
    target_link_libraries(resume_platform PUBLIC shell32 advapi32 ole32)
elseif(APPLE)
    target_sources(resume_platform PRIVATE
                   paths_macos.cpp
                   settings_ini.cpp)
elseif(UNIX)
    target_sources(resume_platform PRIVATE
                   paths_linux.cpp
                   settings_ini.cpp)
else()
    message(FATAL_ERROR "No platform backend for this system")
endif()

target_include_directories(resume_platform PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# PUBLIC on the core: the path type in these interfaces is the core's.
target_link_libraries(resume_platform PUBLIC resume_core)
