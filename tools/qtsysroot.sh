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
x11proto-dev
xorg-sgml-doctools
libgles-dev
libgles2
"

cd "$root/debs"
# shellcheck disable=SC2086
apt-get download $packages > /dev/null 2>&1 || echo "note: some packages could not be downloaded"

for deb in "$root"/debs/*.deb; do
  [ -f "$deb" ] && dpkg-deb -x "$deb" "$root/tree"
done

echo "sysroot at $root/tree"
ls "$root/tree/usr/lib/x86_64-linux-gnu/" 2>/dev/null | grep -E '^lib(OpenGL|GLX|GL|EGL)\.so' | sed 's/^/  /'
ls "$root/tree/usr/include/GL/gl.h" 2>/dev/null | sed 's/^/  /'
