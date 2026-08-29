#!/usr/bin/env bash
# Builds and collects the Linux artifacts, the counterpart of release.ps1.
#
#   tools/release-linux.sh [--no-clean]
#
# Produces, named after the version in res/version.h and filed under it, beside
# the Windows artifacts of the same release:
#
#   releases/<version>/CVBuilder-<version>-linux-x86_64.tar.gz
#   releases/<version>/cvcli-<version>-linux-x86_64      the console renderer alone
#
# The console renderer is published as a bare file as well as inside the archive:
# it needs only libstdc++ and glibc, and for anyone who wants nothing but JSON in
# and PDF out it is the whole program.
#
# How far back these binaries run is decided by the machine they were built on, not
# by a linker flag - that was measured; see the note in src/cli/CMakeLists.txt. The
# script prints the requirement it actually produced so that the release notes can
# state it instead of guessing. Building on the oldest distribution you mean to
# support is the only way to move that number, and the same is true of an AppImage,
# which bundles Qt but never glibc.
#
# The graphical program needs Qt 6.4 or newer from the distribution; that is stated
# in the archive rather than bundled.
set -u
cd "$(dirname "$0")/.."

clean=1
[ "${1:-}" = "--no-clean" ] && clean=0

cmake=$(command -v cmake || true)
if [ -z "$cmake" ] && [ -x "$HOME/.local/bin/cmake" ]; then
    cmake=$HOME/.local/bin/cmake
fi
if [ -z "$cmake" ]; then
    echo "cmake not found: sudo apt install -y cmake" >&2
    exit 1
fi

# The version lives in res/version.h and nowhere else, the same file CMake and the
# Windows installer read it from.
number() { sed -n "s/^#define $1 \\+\\([0-9]\\+\\).*/\\1/p" res/version.h; }
version="$(number VER_MAJOR).$(number VER_MINOR).$(number VER_PATCH)"
channel=$(sed -n 's/^#define VER_CHANNEL "\([^"]*\)".*/\1/p' res/version.h)
if ! printf '%s' "$version" | grep -qE '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "could not read the version out of res/version.h" >&2
    exit 1
fi

# ---------------------------------------------------------------------- build
[ "$clean" -eq 1 ] && rm -rf build/linux
"$cmake" --preset linux || exit 1
"$cmake" --build build/linux -j"$(nproc)" || exit 1

# A published binary that has not been tested is a published binary nobody should
# trust. The Qt case needs no display.
( cd build/linux && QT_QPA_PLATFORM=offscreen ctest --output-on-failure ) || exit 1

bin=build/linux/bin
if [ ! -x "$bin/cvcli" ]; then
    echo "$bin/cvcli was not built" >&2
    exit 1
fi

# --------------------------------------------------------------- collecting
# One directory per release, so a version's artifacts stay together whatever
# platform they were built on.
releases=releases/$version
mkdir -p "$releases"

name="CVBuilder-$version-linux-x86_64"
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/$name"

cp "$bin/cvcli" "$stage/$name/"
[ -x "$bin/CVBuilder-Qt" ] && cp "$bin/CVBuilder-Qt" "$stage/$name/"
cp sample_cv.json README.md LICENSE "$stage/$name/"
cp -r assets "$stage/$name/"

# Without the bundled font the program falls back to a system face and the
# exported PDF quietly stops being the same file it is everywhere else, so an
# archive missing it must not be published.
for required in assets/fonts/LiberationSans-Regular.ttf \
                assets/fonts/LiberationSans-Bold.ttf \
                assets/fonts/LICENSE-LiberationSans.txt; do
    if [ ! -f "$stage/$name/$required" ]; then
        echo "missing from the archive: $required" >&2
        exit 1
    fi
done

# What the binaries actually require, read out of the binaries rather than assumed,
# so the note in the archive cannot drift from the files beside it.
requirement() {
    objdump -T "$1" 2>/dev/null | grep -oE "$2"'_[0-9.]+' | sort -V -u | tail -1
}
glibc=$(requirement "$bin/cvcli" GLIBC)
glibcxx=$(requirement "$bin/cvcli" GLIBCXX)

cat > "$stage/$name/INSTALL.txt" <<EOF
CV Builder $version

  ./cvcli резюме.json резюме.pdf     без графического интерфейса
  ./CVBuilder-Qt                     редактор с предпросмотром

Требования, считанные из самих файлов: $glibc и $glibcxx.
Это Ubuntu 22.04 / Debian 12 и новее. На более старой системе программу нужно
собрать из исходников - сборка описана в README.

CVBuilder-Qt использует Qt из системы, версии 6.4 или новее:

  Debian, Ubuntu   sudo apt install libqt6widgets6 libqt6printsupport6
  Fedora           sudo dnf install qt6-qtbase-gui
  Arch             sudo pacman -S qt6-base

Каталог assets/fonts обязателен и должен лежать рядом с программой: по этому
шрифту свёрстан шаблон, и именно он делает PDF одинаковым на всех системах.
EOF

tar -C "$stage" -czf "$releases/$name.tar.gz" "$name"
cp "$bin/cvcli" "$releases/cvcli-$version-linux-x86_64"

# ------------------------------------------------------------------ summary
echo
echo "Релиз $version собран (канал: $channel)"
[ "$channel" != "release" ] && echo "ВНИМАНИЕ: канал не release - это предрелизная сборка."
echo

for file in "$releases/$name.tar.gz" "$releases/cvcli-$version-linux-x86_64"; do
    size=$(du -h "$file" | cut -f1)
    hash=$(sha256sum "$file" | cut -d' ' -f1)
    printf '  %s\n      %s   sha256 %s\n' "$(basename "$file")" "$size" "$hash"
done
echo
echo "Каталог: $(pwd)/$releases"
