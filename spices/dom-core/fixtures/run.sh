#!/usr/bin/env bash
# fixtures/run.sh -- tur-dom-core's end-to-end fixtures.
#
# fixtures/c/edit_test.c is the C-level unit test for the editing API in
# c/dom/turdom.c (xml-html-parsers-plan follow-up B1).  It is plain C, so it
# lives outside tests/ (`tur test` builds .tur suites), and it is built here
# with -fsanitize=address,undefined so a mutator that corrupts the arena or
# reads freed memory fails the run.  scripts/run-shard.sh runs this after the
# tests stage.
#
# When the compiler cannot link the sanitizer runtimes (a clang without
# compiler-rt, say), the test still runs, unsanitized, and the run says so.
#
# Usage: fixtures/run.sh [path/to/tur]      # the tur argument is unused
#        CC=clang fixtures/run.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

CC="${CC:-cc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

SRC=(fixtures/c/edit_test.c c/dom/turdom.c)
CFLAGS=(-std=c11 -g -O1 -Wall -Wextra -Werror -I c)
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)

echo "1..1"
if "$CC" "${CFLAGS[@]}" "${SAN[@]}" "${SRC[@]}" -o "$OUT/edit_test" 2>"$OUT/cc.log"; then
  san="asan+ubsan"
else
  echo "# WARNING: $CC could not build with ${SAN[*]}; running unsanitized:"
  sed 's/^/#   /' "$OUT/cc.log" | head -5
  if ! "$CC" "${CFLAGS[@]}" "${SRC[@]}" -o "$OUT/edit_test" 2>"$OUT/cc.log"; then
    echo "not ok 1 - edit_test (build failed)"
    sed 's/^/#   /' "$OUT/cc.log" | head -20
    exit 1
  fi
  san="unsanitized"
fi

if ASAN_OPTIONS="detect_leaks=1:${ASAN_OPTIONS:-}" "$OUT/edit_test" > "$OUT/run.log" 2>&1; then
  echo "ok 1 - edit_test ($san) $(tail -1 "$OUT/run.log")"
  echo "# All 1 fixtures passed."
else
  echo "not ok 1 - edit_test ($san)"
  sed 's/^/#   /' "$OUT/run.log" | head -40
  echo "# 1 of 1 fixtures failed."
  exit 1
fi
