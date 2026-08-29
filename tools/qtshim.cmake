# Makes Qt6::Gui linkable on a machine where the OpenGL development files are not
# installed system-wide - a minimal container, or a WSL distribution kept for
# cross-compiling. Qt's own CMake config requires the OpenGL imported targets even
# for a program that never draws with OpenGL, which a Widgets application does not.
#
# Not needed on a normal desktop: install qt6-base-dev (or the Qt installer's own
# package, which pulls Mesa in) and ignore this file. Used as:
#
#   tools/qtsysroot.sh                     # unpacks the packages, no sudo
#   cmake --preset linux-qt -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=tools/qtshim.cmake
#
# The targets are declared here rather than left to FindOpenGL because they only
# have to exist: nothing in this program calls an OpenGL function.
set(_cvb_sysroot "$ENV{HOME}/qtsysroot/tree")
set(_cvb_gl_libs "${_cvb_sysroot}/usr/lib/x86_64-linux-gnu")

if(EXISTS "${_cvb_sysroot}/usr/include/GL/gl.h")
    foreach(entry "GL:libGL.so" "OpenGL:libOpenGL.so" "GLX:libGLX.so" "EGL:libEGL.so")
        string(REPLACE ":" ";" entry "${entry}")
        list(GET entry 0 name)
        list(GET entry 1 file)
        if(NOT TARGET OpenGL::${name} AND EXISTS "${_cvb_gl_libs}/${file}")
            add_library(OpenGL::${name} UNKNOWN IMPORTED)
            set_target_properties(OpenGL::${name} PROPERTIES
                                  IMPORTED_LOCATION "${_cvb_gl_libs}/${file}"
                                  INTERFACE_INCLUDE_DIRECTORIES
                                      "${_cvb_sysroot}/usr/include")
        endif()
    endforeach()

    # So that the find_package(OpenGL) Qt performs is satisfied by the same files
    # rather than searching the system and failing.
    set(OPENGL_INCLUDE_DIR "${_cvb_sysroot}/usr/include" CACHE PATH "" FORCE)
    set(OPENGL_gl_LIBRARY "${_cvb_gl_libs}/libGL.so" CACHE FILEPATH "" FORCE)
    set(OPENGL_opengl_LIBRARY "${_cvb_gl_libs}/libOpenGL.so" CACHE FILEPATH "" FORCE)
    set(OPENGL_glx_LIBRARY "${_cvb_gl_libs}/libGLX.so" CACHE FILEPATH "" FORCE)
    set(OPENGL_egl_LIBRARY "${_cvb_gl_libs}/libEGL.so" CACHE FILEPATH "" FORCE)
    set(OpenGL_FOUND TRUE)
    set(OPENGL_FOUND TRUE)

    message(STATUS "OpenGL taken from the private sysroot at ${_cvb_sysroot}")
endif()
