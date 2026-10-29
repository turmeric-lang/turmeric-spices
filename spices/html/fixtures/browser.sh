#!/usr/bin/env bash
# fixtures/browser.sh -- markup literals' code holes in a real browser
# (xml-html-followups-plan A6): browser/pages.tur renders one page per
# hostile value with the value in every JavaScript, JSON, CSS and srcdoc
# context, and browser/check.js loads them in headless Chromium, clicks the
# handlers, and checks that no value ran code and every string hole reads
# back as the value.
#
# Needs node with the playwright package and its Chromium; without them it
# says so and passes (skipped), as CI runners without a browser do.
#
# Usage: fixtures/browser.sh [path/to/tur]
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR_BIN="${1:-${TUR_BIN:-tur}}"
cd "$ROOT" || exit 2

if ! command -v node >/dev/null 2>&1 ||
   ! (cd fixtures/browser && node -e "require('playwright')" >/dev/null 2>&1); then
  echo "# skipped: node with playwright is not available"
  exit 0
fi

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
if ! TUR_BROWSER_OUT="$OUT" "$TUR_BIN" run fixtures/browser/pages.tur; then
  echo "# could not build the pages"
  exit 1
fi
(cd fixtures/browser && node check.js "$OUT")
