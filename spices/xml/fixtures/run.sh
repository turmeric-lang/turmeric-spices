#!/usr/bin/env bash
# fixtures/run.sh -- tur-xml's end-to-end fixtures: the W3C conformance slice,
# the mutation fuzz pass, and the dom/edit fuzz pass.
#
# Both are whole programs with their own `main`, so they live outside tests/
# (`tur test` recurses and would count them as suites).  scripts/run-shard.sh
# runs this after the tests stage.
#
# The conformance fixture needs James Clark's xmltest from the W3C XML
# Conformance Test Suite.  It is not vendored -- its licence only permits
# redistributing the unmodified archive -- so it is fetched here from W3C,
# checked against a pinned SHA-256 and unpacked into fixtures/.xmlconf/
# (gitignored).  Offline, point XMLCONF_TARBALL at a local copy of the same
# file.  A failed DOWNLOAD skips the fixture with a warning (W3C's server is
# not ours to make CI depend on); a checksum MISMATCH fails it.
#
# Usage: fixtures/run.sh [path/to/tur]      # from anywhere
#        TUR_BIN=... fixtures/run.sh
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TUR_BIN="${1:-${TUR_BIN:-tur}}"
cd "$ROOT" || exit 2

XMLTS_URL="https://www.w3.org/XML/Test/xmlts20130923.tar.gz"
XMLTS_SHA256="9b61db9f5dbffa545f4b8d78422167083a8568c59bd1129f94138f936cf6fc1f"
CACHE="fixtures/.xmlconf"

fails=0

sha256_of() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

# Prints "ok", "skip <why>" or "fail <why>".
fetch_xmlconf() {
  if [ -d "$CACHE/xmlconf/xmltest/valid/sa" ]; then
    echo ok
    return
  fi
  mkdir -p "$CACHE"
  local tarball="${XMLCONF_TARBALL:-$CACHE/xmlts20130923.tar.gz}"
  if [ ! -f "$tarball" ]; then
    if ! curl -fsSL --retry 2 -o "$tarball.part" "$XMLTS_URL" 2>/dev/null; then
      rm -f "$tarball.part"
      echo "skip could not download $XMLTS_URL"
      return
    fi
    mv "$tarball.part" "$tarball"
  fi
  local got
  got=$(sha256_of "$tarball")
  if [ "$got" != "$XMLTS_SHA256" ]; then
    echo "fail $tarball has SHA-256 $got, expected $XMLTS_SHA256"
    return
  fi
  if ! tar -xzf "$tarball" -C "$CACHE" xmlconf/xmltest/valid/sa xmlconf/xmltest/not-wf/sa; then
    echo "fail could not unpack $tarball"
    return
  fi
  echo ok
}

echo "== conformance (xmltest valid/sa + not-wf/sa)"
status=$(fetch_xmlconf)
case "$status" in
  ok)
    if ! "$TUR_BIN" run fixtures/conformance.tur; then
      fails=$((fails + 1))
    fi
    ;;
  skip*)
    echo "::warning title=xml: conformance skipped::${status#skip }"
    echo "# SKIP conformance: ${status#skip }"
    ;;
  *)
    echo "::error title=xml: conformance suite::${status#fail }"
    fails=$((fails + 1))
    ;;
esac

echo "== fuzz (mutated inputs through every entry point)"
if ! "$TUR_BIN" run fixtures/fuzz.tur; then
  fails=$((fails + 1))
fi

echo "== edit fuzz (random dom/edit sequences, refusals and round trips)"
if ! "$TUR_BIN" run fixtures/edit-fuzz.tur; then
  fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
  echo "# $fails fixture group(s) failed."
  exit 1
fi
echo "# All fixture groups passed."
