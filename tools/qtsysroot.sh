#!/usr/bin/env bash
# Puts the OpenGL and X11 development files Qt6::Gui links against into a private
# sysroot, without touching the system and without sudo: the distribution packages
# are downloaded and unpacked into a directory of their own.
#
# Only needed on a machine where those packages are not installed - a CI image, or
# a WSL distribution used for cross-compiling. On a normal desktop with
# qt6-base-dev installed none of this applies.
set -u
root=${1:-$HOME/qtsysroot}
mkdir -p "$root/debs" "$root/tree"

# The first group is what Qt6::Gui needs to *link*. The second is what its xcb
# platform plugin needs to actually open a window, which is only interesting on a
# machine where the program is meant to be looked at - under WSLg, for instance.
packages="
mesa-common-dev
libglvnd-dev
libglvnd0
libopengl0
libopengl-dev
libglx0
libglx-dev
libgl1
libgl-dev
libegl1
libegl-dev
libx11-6
libx11-dev
libxext6
libxext-dev
libxau6
libxdmcp6
libxcb1
libxcb1-dev
libbsd0
libmd0
x11proto-dev
xorg-sgml-doctools
libgles-dev
libgles2
libxcb-cursor0
libxcb-icccm4
libxcb-image0
libxcb-keysyms1
libxcb-randr0
libxcb-render-util0
libxcb-render0
libxcb-shape0
libxcb-shm0
libxcb-sync1
libxcb-util1
libxcb-xfixes0
libxcb-xinerama0
libxcb-xkb1
libxkbcommon0
libxkbcommon-x11-0
libxrender1
libice6
libsm6
libfontconfig1
libfreetype6
libpng16-16t64
libbrotlicommon1
libbrotlidec1
libgraphite2-3
libharfbuzz0b
libglib2.0-0t64
libdbus-1-3
libexpat1
libuuid1
libzstd1
libpcre2-8-0
libffi8
"

cd "$root/debs"
# One at a time on purpose: apt-get download fails the whole invocation over a
# single unknown name, and package names drift between releases. A missing one
# should cost that library, not the entire sysroot.
missing=""
for package in $packages; do
  apt-get download "$package" > /dev/null 2>&1 || missing="$missing $package"
done
if [ -n "$missing" ]; then
  echo "not available under these names:$missing"
fi

for deb in "$root"/debs/*.deb; do
  [ -f "$deb" ] && dpkg-deb -x "$deb" "$root/tree"
done

echo "sysroot at $root/tree"
ls "$root/tree/usr/lib/x86_64-linux-gnu/" 2>/dev/null | grep -E '^lib(OpenGL|GLX|GL|EGL)\.so' | sed 's/^/  /'
ls "$root/tree/usr/include/GL/gl.h" 2>/dev/null | sed 's/^/  /'
