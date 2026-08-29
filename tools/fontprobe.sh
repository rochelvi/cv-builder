#!/usr/bin/env bash
# Compares how the same resume lays out with different faces.
#
# The layout dump is the whole answer: if two fonts produce identical dumps then
# every line break, every baseline and every page break is the same, so the two
# PDFs differ only in the font program embedded in them. Two faces being
# "metric-compatible" by reputation is not evidence; this is.
#
# Usage:
#   tools/fontprobe.sh <cvcli> <name>=<regular.ttf>,<bold.ttf> ...
#
# The first pair given is the reference the rest are compared against, e.g.
#   tools/fontprobe.sh build/linux/bin/cvcli \
#       arial=/tmp/arial.ttf,/tmp/arialbd.ttf \
#       liberation=/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf,/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf
set -u

if [ "$#" -lt 2 ]; then
  sed -n '2,17p' "$0"
  exit 2
fi

cli=$1
shift
out=${TMPDIR:-/tmp}/fontprobe
mkdir -p "$out"

reference=""
names=()

for spec in "$@"; do
  name=${spec%%=*}
  pair=${spec#*=}
  regular=${pair%%,*}
  bold=${pair#*,}

  if [ ! -f "$regular" ] || [ ! -f "$bold" ]; then
    printf '%-14s skipped, font files missing\n' "$name"
    continue
  fi

  "$cli" sample_cv.json "$out/$name.pdf" --dump-ops "$out/$name.ops" \
         --fonts "$regular" "$bold" > /dev/null || continue

  runs=$(grep -c '"op":"text"' "$out/$name.ops")
  size=$(wc -c < "$out/$name.pdf")
  printf '%-14s text runs=%-6s pdf=%s bytes\n' "$name" "$runs" "$size"

  names+=("$name")
  if [ -z "$reference" ]; then reference=$name; fi
done

if [ -z "$reference" ]; then exit 1; fi

echo
echo "layout compared against $reference:"
for name in "${names[@]}"; do
  if [ "$name" = "$reference" ]; then continue; fi
  if cmp -s "$out/$reference.ops" "$out/$name.ops"; then
    printf '  %-12s IDENTICAL layout\n' "$name"
  else
    changed=$(diff "$out/$reference.ops" "$out/$name.ops" | grep -c '^[<>]')
    printf '  %-12s %s dump lines differ\n' "$name" "$changed"
  fi
done
