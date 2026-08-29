#!/usr/bin/env bash
# Checks that a Qt 6 Widgets program can be configured, compiled and linked with
# whatever Qt is on this machine. Purely a toolchain probe - it builds a throwaway
# program, not part of the project.
set -u
qt=${1:-$HOME/Qt/6.8.3/gcc_64}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/main.cpp" <<'EOF'
#include <QApplication>
#include <QPainter>
#include <QPrinter>
#include <QRawFont>
#include <QWidget>
#include <cstdio>

int main(int argc, char** argv) {
    // No window is shown; the point is that all four modules link.
    QApplication app(argc, argv);
    std::printf("qt %s ok\n", qVersion());
    return 0;
}
EOF

cat > "$work/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.21)
project(qtprobe LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets PrintSupport)
qt_standard_project_setup()
qt_add_executable(qtprobe main.cpp)
target_link_libraries(qtprobe PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport)
EOF

cmake=$(command -v cmake || echo "$HOME/.local/bin/cmake")

# On a machine where the OpenGL development files are not installed, point CMake
# at the private sysroot tools/qtsysroot.sh builds. Harmless when it is absent.
sysroot=$HOME/qtsysroot/tree
extra=()
if [ -f "$sysroot/usr/include/GL/gl.h" ]; then
  here=$(cd "$(dirname "$0")" && pwd)
  extra=(-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES="$here/qtshim.cmake")
fi

"$cmake" -S "$work" -B "$work/build" -DCMAKE_PREFIX_PATH="$qt" "${extra[@]}" \
         > "$work/configure.log" 2>&1 || {
  echo "configure failed:"; tail -20 "$work/configure.log"; exit 1; }
"$cmake" --build "$work/build" > "$work/build.log" 2>&1 || {
  echo "build failed:"; tail -25 "$work/build.log"; exit 1; }

echo "configured and linked against $qt"
QT_QPA_PLATFORM=offscreen "$work/build/qtprobe" || echo "(built, but would not run headless)"
