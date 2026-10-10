#!/usr/bin/env bash
# check-sclang.sh -- parse every <name>.expected.scd with a real sclang.
#
# Usage: tests/corpus/check-sclang.sh [--lhc]
#
# Compiles (does not run) each file with Interpreter:compile under stock
# SuperCollider's sclang. A program SOURCES marks "parses" must parse; one
# marked "invalid-input" must not (if it starts parsing, update SOURCES).
# --lhc checks the lhc.js reference outputs instead and only reports.
#
# No sclang on PATH (or $SCLANG): prints SKIP and exits 0.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
sclang="${SCLANG:-$(command -v sclang || true)}"
if [ -z "$sclang" ]; then
  echo "SKIP: no sclang"
  exit 0
fi
suffix=expected
[ "${1:-}" = "--lhc" ] && suffix=lhc

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cat > "$work/check.scd" <<'SCD'
thisProcess.argv.do { |p|
  var f = thisProcess.interpreter.compile("(\n" ++ File.readAllString(p) ++ "\n)");
  ((if(f.isNil) { "PARSE-FAIL " } { "PARSE-OK " }) ++ p).postln;
};
0.exit;
SCD

files=()
while read -r name _src status _reason; do
  case "$name" in ''|'#'*) continue ;; esac
  files+=("$here/$name.$suffix.scd")
done < "$here/SOURCES"

# Headless: no display, and QtWebEngine refuses to start as root without this.
QT_QPA_PLATFORM=offscreen QTWEBENGINE_DISABLE_SANDBOX=1 \
  timeout 120 "$sclang" "$work/check.scd" "${files[@]}" > "$work/out.log" 2>&1

fail=0
while read -r name _src status _reason; do
  case "$name" in ''|'#'*) continue ;; esac
  f="$here/$name.$suffix.scd"
  if grep -qxF "PARSE-OK $f" "$work/out.log"; then got=parses
  elif grep -qxF "PARSE-FAIL $f" "$work/out.log"; then got=fails
  else got=missing; fi
  if [ "$suffix" = lhc ]; then
    echo "$got $name"
  elif [ "$status" = parses ] && [ "$got" = parses ]; then
    echo "ok   $name"
  elif [ "$status" = invalid-input ] && [ "$got" = fails ]; then
    echo "ok   $name (invalid input, does not parse, as expected)"
  else
    echo "FAIL $name: SOURCES says $status, sclang $got"
    fail=$((fail + 1))
  fi
done < "$here/SOURCES"

if [ "$fail" -gt 0 ]; then
  echo "$fail failed; sclang output:"; grep -v '^Superclass\|^/usr' "$work/out.log" | tail -40
fi
exit "$fail"
