#!/usr/bin/env bash
# fixtures/run.sh -- tur-dom-core's end-to-end fixtures.
#
# fixtures/c/*_test.c are the C-level unit tests for c/dom/turdom.c: the
# editing API (edit_test.c, xml-html-parsers-plan follow-up B1) and the
# streaming writer (writer_test.c, B4).  They are plain C, so they live
# outside tests/ (`tur test` builds .tur suites), and they are built here with
# -fsanitize=address,undefined so a mutator that corrupts the arena or reads
# freed memory fails the run.  scripts/run-shard.sh runs this after the tests
# stage.
#
# When the compiler cannot link the sanitizer runtimes (a clang without
# compiler-rt, say), the tests still run, unsanitized, and the run says so.
#
# Usage: fixtures/run.sh [path/to/tur]      # the tur argument is unused
#        CC=clang fixtures/run.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 2

CC="${CC:-cc}"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

TESTS=(edit_test writer_test)
CFLAGS=(-std=c11 -g -O1 -Wall -Wextra -Werror -I c)
SAN=(-fsanitize=address,undefined -fno-sanitize-recover=all)

# Leak checking where LeakSanitizer exists.  Apple's ASan has none, and
# detect_leaks=1 makes it abort at startup ("not supported on this platform").
RUN_ASAN="${ASAN_OPTIONS:-}"
if [ "$(uname -s)" = Linux ]; then
  RUN_ASAN="detect_leaks=1${RUN_ASAN:+:$RUN_ASAN}"
fi

echo "1..${#TESTS[@]}"
fails=0
i=0
for t in "${TESTS[@]}"; do
  i=$((i + 1))
  src=("fixtures/c/$t.c" c/dom/turdom.c)
  if "$CC" "${CFLAGS[@]}" "${SAN[@]}" "${src[@]}" -o "$OUT/$t" 2>"$OUT/cc.log"; then
    san="asan+ubsan"
  else
    echo "# WARNING: $CC could not build $t with ${SAN[*]}; running unsanitized:"
    sed 's/^/#   /' "$OUT/cc.log" | head -5
    if ! "$CC" "${CFLAGS[@]}" "${src[@]}" -o "$OUT/$t" 2>"$OUT/cc.log"; then
      echo "not ok $i - $t (build failed)"
      sed 's/^/#   /' "$OUT/cc.log" | head -20
      fails=$((fails + 1))
      continue
    fi
    san="unsanitized"
  fi
  if ASAN_OPTIONS="$RUN_ASAN" "$OUT/$t" "$OUT" > "$OUT/run.log" 2>&1; then
    echo "ok $i - $t ($san) $(tail -1 "$OUT/run.log")"
  else
    echo "not ok $i - $t ($san)"
    sed 's/^/#   /' "$OUT/run.log" | head -40
    fails=$((fails + 1))
  fi
done

if [ "$fails" -ne 0 ]; then
  echo "# $fails of ${#TESTS[@]} fixtures failed."
  exit 1
fi
echo "# All ${#TESTS[@]} fixtures passed."
