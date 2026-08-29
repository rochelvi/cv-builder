#!/usr/bin/env bash
# Configures and builds a preset inside WSL. Called by build.ps1.
#
#   bash tools/build-windows.sh [preset]
#
# A script rather than a one-liner passed to `wsl -- bash -c`: PowerShell 7
# rewrites embedded quotes when it hands arguments to a native program, so a
# command line with quoting in it arrives mangled. Nothing here needs quoting to
# survive that trip.
set -u
cd "$(dirname "$0")/.."

preset=${1:-windows-mingw}

# cmake may be an apt package on PATH or a tarball unpacked into ~/.local; take
# whichever is there rather than insisting on one.
cmake=$(command -v cmake || true)
if [ -z "$cmake" ] && [ -x "$HOME/.local/bin/cmake" ]; then
    cmake=$HOME/.local/bin/cmake
fi
if [ -z "$cmake" ]; then
    echo "cmake not found: sudo apt install -y cmake" >&2
    exit 1
fi

"$cmake" --preset "$preset" || exit 1
"$cmake" --build "build/$preset" -j"$(nproc)" || exit 1
