# Renders a CV to PDF with no window in sight, which also makes it the tool the
# regression tests drive.
add_executable(cvcli cli.cpp)
target_link_libraries(cvcli PRIVATE resume_core resume_app resume_version)
cvb_stage_assets(cvcli)

# Note for whoever thinks of linking libstdc++ statically here to make the console
# renderer a single portable file: it was tried, and measured, and it makes
# portability worse. Linked against the shared one the binary needs GLIBC 2.34 and
# GLIBCXX 3.4.29 - Ubuntu 22.04, Debian 12. Statically, libstdc++'s own objects
# drag in symbols from whatever glibc built them, and the requirement rose to
# GLIBC 2.38, which is a year and a half newer.
#
# What actually decides how far back a Linux binary runs is the age of the machine
# it was built on, not a linker flag. A broadly compatible build - or an AppImage,
# which bundles Qt but never glibc - has to be produced on an old base; see
# tools/release-linux.sh.

if(WIN32)
    target_sources(cvcli PRIVATE "${PROJECT_SOURCE_DIR}/res/cli.rc")
    target_link_libraries(cvcli PRIVATE shell32)
endif()
