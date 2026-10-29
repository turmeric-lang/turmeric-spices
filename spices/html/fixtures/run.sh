#!/usr/bin/env bash
# fixtures/run.sh -- tur-html's end-to-end fixtures: the mutation fuzz pass,
# the dom/edit fuzz pass, and the browser check of code holes.
#
# A whole program with its own `main`, so it lives outside tests/ (`tur test`
# recurses and would count it as a suite).  scripts/run-shard.sh runs this
# after the tests stage.  The html5lib tree-construction slice is vendored
# (MIT) and runs as an ordinary test: tests/html5lib.tur.
#
# Usage: fixtures/run.sh [path/to/tur]      # from anywhere
#        TUR_BIN=... fixtures/run.sh
#        TUR_FUZZ_ITERS=100000 TUR_FUZZ_SEED=7 fixtures/run.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR_BIN="${1:-${TUR_BIN:-tur}}"
cd "$ROOT" || exit 2

echo "== fuzz (mutated inputs through every entry point)"
if ! "$TUR_BIN" run fixtures/fuzz.tur; then
  echo "# fuzz failed."
  exit 1
fi

echo "== edit fuzz (random dom/edit sequences, refusals and round trips)"
if ! "$TUR_BIN" run fixtures/edit-fuzz.tur; then
  echo "# edit fuzz failed."
  exit 1
fi
echo "== browser (code holes in headless Chromium)"
if ! bash fixtures/browser.sh "$TUR_BIN"; then
  echo "# browser check failed."
  exit 1
fi
echo "# All fixture groups passed."
