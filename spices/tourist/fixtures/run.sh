#!/usr/bin/env bash
# fixtures/run.sh -- run every tourist end-to-end fixture and report TAP.
#
# Each fixture is a whole program: it starts a tourist app on a
# kernel-assigned port, drives it with the http client spice, and exits with
# its test runner's status (run-all-status), so a failing assertion fails the
# fixture.  They live outside tests/ because each has its own `main`; the
# compile-FAIL fixture lives in errors/.
#
# Nothing ran this directory before, and two fixtures had quietly stopped
# compiling or crashed.  The programs link libturi, so on a sanitized (Debug)
# tur they run under ASan with leak detection on: a leak fails the fixture.
#
# run-shard.sh's fixtures stage calls this; it is a no-op for any spice
# without an executable fixtures/run.sh.
#
# Usage: fixtures/run.sh [path/to/tur]      # from anywhere
#        TUR_BIN=... fixtures/run.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR_BIN="${1:-${TUR_BIN:-tur}}"
# Seconds per fixture, build included.  `perl -e 'alarm N; exec @ARGV'`
# rather than `timeout N`: macOS ships no coreutils timeout.
LIMIT="${TOURIST_FIXTURE_TIMEOUT:-180}"

cd "$ROOT" || exit 2

fixtures=(hello-world path-capture query-param post-echo middleware
          static-files url-map cascade nested-routing ctx-attrs-after template)
fails=0
n=0
log="$(mktemp)"
trap 'rm -f "$log"' EXIT

echo "1..${#fixtures[@]}"
for f in "${fixtures[@]}"; do
  n=$((n + 1))
  src="fixtures/${f}/${f}.tur"
  if [ ! -f "$src" ]; then
    echo "not ok ${n} - ${f} (missing ${src})"
    fails=$((fails + 1))
    continue
  fi
  # In a subshell, so bash's "Alarm clock" notice for a timed-out fixture
  # lands in the log rather than on the TAP stream.
  ( perl -e 'alarm shift; exec @ARGV' "$LIMIT" "$TUR_BIN" run "$src"; exit $? ) > "$log" 2>&1
  rc=$?
  summary="$(grep -E '^# (All [0-9]+ tests passed|[0-9]+ passed, [0-9]+ failed)' "$log" | tail -1)"
  if [ "$rc" -eq 0 ]; then
    echo "ok ${n} - ${f} ${summary}"
  else
    if [ "$rc" -eq 142 ]; then why="timed out (>${LIMIT}s)"; else why="exit ${rc}"; fi
    echo "not ok ${n} - ${f} (${why}) ${summary}"
    # The failing assertions, or the build / sanitizer error.
    grep -E '^not ok|FAIL|error|ERROR|leak of|SUMMARY' "$log" | head -20 | sed 's/^/#   /'
    fails=$((fails + 1))
  fi
done

if [ "$fails" -eq 0 ]; then
  echo "# All ${#fixtures[@]} fixtures passed."
else
  echo "# ${fails} of ${#fixtures[@]} fixtures failed."
  exit 1
fi
