#!/usr/bin/env bash
# bench/run.sh -- the crdt join benchmark.
#
# crdt-spice-plan section 7 asked for a baseline of HAMT join cost at the
# sizes a real document hits. bench/join.tur times three merges at n = 16 to
# 65536 (best of several repetitions each); this script runs it, checks
# every row's checksum against the arithmetic below, and prints the table
# with a per-entry cost.
#
#   gcounter-join  a holds replicas 0..n-1 at 1, b holds n/2..3n/2-1 at 2:
#                  the merged value is 2.5n
#   orset-join     elements 0..n-1 and n/2..3n/2-1: 1.5n distinct
#   orset-apply    an n-element set plus a one-element add delta: n+1
#
# A row whose checksum is wrong is reported and fails the run, so a "fast"
# row cannot be one that computed the wrong merge.
#
# Takes about two minutes; most of it is building the 65536-entry inputs.
#
# Usage: bench/run.sh [path/to/tur]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SPICE_ROOT="$(cd "$HERE/.." && pwd)"
TUR="${1:-${TUR:-tur}}"

cd "$SPICE_ROOT"

out="$("$TUR" run bench/join.tur 2>/dev/null)" || {
  echo "bench/join.tur did not build or run"; exit 1; }

printf '%-14s %7s %5s %12s %10s  %s\n' ROW N REPS "BEST US" "NS/ENTRY" CHECKSUM
fail=0
while read -r kind n reps us sum; do
  case "$kind" in
    gcounter-join) want=$((n * 5 / 2)) ;;
    orset-join)    want=$((n * 3 / 2)) ;;
    orset-apply)   want=$((n + 1)) ;;
    *) continue ;;
  esac
  per="$(awk -v u="$us" -v n="$n" 'BEGIN{printf "%.0f", u*1000/n}')"
  note="$sum"
  if [ "$sum" != "$want" ]; then note="WRONG ($sum, want $want)"; fail=1; fi
  printf '%-14s %7s %5s %12s %10s  %s\n' "$kind" "$n" "$reps" "$us" "$per" "$note"
done <<<"$out"

if [ "$fail" -ne 0 ]; then
  echo "# one or more rows merged to the wrong answer"
  exit 1
fi
echo "# every row's checksum matches"
