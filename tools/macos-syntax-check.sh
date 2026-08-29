#!/usr/bin/env bash
# Compiles the macOS-only sources for syntax on a machine that is not a Mac.
#
# This is not a build and it is not a substitute for one: it cannot link, cannot
# run, and cannot tell whether _NSGetExecutablePath behaves as expected. What it
# does catch is the class of mistake that would otherwise sit in the repository
# unnoticed until somebody with a Mac tried - a typo, a missing include, a wrong
# type - which for a file nobody on this machine can compile is worth having.
#
# The one Apple header these files need is stubbed out with the signature from
# Apple's own documentation.
set -u
cd "$(dirname "$0")/.."

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/mach-o"

cat > "$work/mach-o/dyld.h" <<'EOF'
// Stub of the one declaration the macOS path code uses, for a syntax check on a
// machine with no Apple SDK.
#pragma once
#include <cstdint>
extern "C" int _NSGetExecutablePath(char* buf, uint32_t* bufsize);
EOF

status=0
for file in src/platform/paths_macos.cpp src/platform/font_source_macos.cpp; do
  if g++ -std=gnu++17 -fsyntax-only -Wall -Wextra -D__APPLE__ \
         -I "$work" -I src/core -I src/platform "$file" 2> "$work/log"; then
    printf '%-40s syntax ok\n' "$file"
  else
    printf '%-40s FAILED\n' "$file"
    sed 's/^/    /' "$work/log"
    status=1
  fi
done

exit "$status"
