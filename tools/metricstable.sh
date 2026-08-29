#!/usr/bin/env bash
# Prints the advance widths the test table pins, taken from a reference font, so
# the expected numbers are measured rather than remembered.
#
#   tools/metricstable.sh <cvcli> <regular.ttf> <bold.ttf>
set -u
if [ "$#" -ne 3 ]; then sed -n '2,6p' "$0"; exit 2; fi

cli=$1
report=$("$cli" --font-report --fonts "$2" "$3")

for cp in 0020 0041 004D 0061 0069 006C 006D 0077 0030 002D 002E 002C 002F 0040 \
          0410 0430 044F 0451 00B7 203A 2014 2026; do
  regular=$(echo "$report" | awk -v want="U+$cp" '
    /^\[regular\]/ {section="regular"} /^\[bold\]/ {section="bold"}
    section=="regular" && $1==want {print $3; exit}')
  bold=$(echo "$report" | awk -v want="U+$cp" '
    /^\[bold\]/ {section="bold"}
    section=="bold" && $1==want {print $3; exit}')
  # Rounded, not truncated: the test compares rounded thousandths of an em, and
  # 277.832 is 278 there.
  printf '        {0x%s, %.0f, %.0f},\n' "$cp" "$regular" "$bold"
done
