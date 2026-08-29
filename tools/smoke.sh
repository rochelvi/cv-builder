#!/usr/bin/env bash
# Starts the portable front end headless for a few seconds and reports whether it
# stayed up. Catches the failures that a compile cannot: a missing font asset, a
# crash while the first document is laid out, a Qt warning on start-up.
set -u
cd "$(dirname "$0")/.."

app=${1:-build/linux/bin/CVBuilder-Qt}
log=${TMPDIR:-/tmp}/cvb-smoke.log

QT_QPA_PLATFORM=offscreen timeout 5 "$app" > "$log" 2>&1
code=$?

# 124 is timeout's own code: the program was still running when time ran out,
# which for a GUI is exactly what success looks like.
if [ "$code" -eq 124 ]; then
  echo "started and stayed up"
else
  echo "exited on its own with code $code"
fi

if [ -s "$log" ]; then
  echo "--- output:"
  cat "$log"
else
  echo "no warnings on stderr"
fi

[ "$code" -eq 124 ]
