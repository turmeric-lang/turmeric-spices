#!/usr/bin/env bash
# errors/run.sh -- `#xml{...}` holes in forbidden places are compile errors
# (xml-html-followups-plan follow-up A1, XML flavour).
#
# Each case is one literal, compiled through `xml*` (the macro behind
# `#xml{...}`) with `tur check`; it must be REJECTED with a message naming
# the place.  Pinning the witness keeps a case from drifting onto an
# unrelated diagnostic and passing without testing anything.  The
# generated files live in a temporary directory under the spice, so the
# spice's own src/ (and dom-core) resolve as they would for a user.
#
# The last two cases are run-time ones: a raw-html value that is not
# well-formed XML content, and a literal that is not well-formed, must
# panic rather than be written.
#
# scripts/run-shard.sh runs this after the tests stage.
#
# Usage: errors/run.sh [path/to/tur]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SPICE_ROOT="$(cd "$HERE/.." && pwd)"
TUR="${1:-${TUR:-tur}}"
cd "$SPICE_ROOT" || exit 2

TMP="$(mktemp -d "$HERE/tmp.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fail=0
n=0

# expect_reject <literal> <witness>
#
# The literal is a Turmeric string body: write \" for a double quote.  The
# hole expressions refer to `x` (a cstr).
expect_reject() {
  local lit="$1" witness="$2" f out
  n=$((n + 1))
  f="$TMP/case$n.tur"
  cat > "$f" <<EOF
(defmodule case$n
  (import xml/markup :refer [xml*])
  (import dom/holes :refer [xml->string])
  (defn main [] : int
    (let [x "v"]
      (println (xml->string (xml* "$lit"))))
    0))
EOF
  out="$("$TUR" check "$f" 2>&1)"
  if printf '%s' "$out" | grep -qF -- "$witness"; then
    echo "ok $n - $lit"
  else
    echo "not ok $n - $lit"
    echo "    expected a diagnostic containing: $witness"
    printf '%s\n' "$out" | grep -m3 'error' | sed 's/^/      /'
    fail=1
  fi
}

# expect_panic <name> <file body> <witness>: a program that must compile and
# then panic with the witness.
expect_panic() {
  local name="$1" body="$2" witness="$3" f out
  n=$((n + 1))
  f="$TMP/run$n.tur"
  printf '%s\n' "$body" > "$f"
  out="$("$TUR" run "$f" 2>&1)"
  if printf '%s' "$out" | grep -qF -- "$witness"; then
    echo "ok $n - $name"
  else
    echo "not ok $n - $name"
    printf '%s\n' "$out" | tail -5 | sed 's/^/      /'
    fail=1
  fi
}

echo "# holes where a value could change the document's structure"
expect_reject '<${x}/>' "a hole cannot be a tag name"
expect_reject '<r${x}/>' "a hole cannot be part of a tag name"
expect_reject '<r></r${x}>' "a hole cannot be part of an end tag"
expect_reject '<r ${x}/>' "a hole cannot be an attribute name"
expect_reject '<r a${x}=\"1\"/>' "a hole cannot be part of an attribute name"
expect_reject '<r a=b${x}/>' "shares an unquoted attribute value"
expect_reject '<r><!-- ${x} --></r>' "a hole inside a comment"
expect_reject '<r><![CDATA[${x}]]></r>' "a hole inside a CDATA section"
expect_reject '<r><?pi ${x}?></r>' "a hole inside a processing instruction"
expect_reject '<!DOCTYPE r [${x}]><r/>' "a hole inside a doctype"
echo "# attributes whose value is code or markup (plan A-4: SVG and XHTML run them)"
expect_reject '<svg onload=\"${x}\"/>' "event-handler attribute 'onload'"
expect_reject '<r style=${x}/>' "a hole in the 'style' attribute"
echo "# the literal itself"
expect_reject '<r>${}</r>' "an empty hole"
expect_reject '<r>__tur_hole_0__</r>' "'__tur_hole_' is reserved"
echo "# types: the position picks the class"
expect_reject '<r a=${(xml* \"<b/>\")}/>' "ToAttr"

echo "# run time"
expect_panic "a raw value that is not well-formed XML content panics" '(defmodule rawcase
  (import xml/markup :refer [xml*])
  (import dom/holes :refer [xml->string raw-html])
  (defn main [] : int
    (println (xml->string (xml* "<r>${(raw-html \"<a>\")}</r>")))
    0))' "not well-formed XML content"
expect_panic "a literal that is not well-formed panics on its first render" '(defmodule wfcase
  (import xml/markup :refer [xml*])
  (import dom/holes :refer [xml->string])
  (defn main [] : int
    (println (xml->string (xml* "<a><b>${\"x\"}</a>")))
    0))' "literal is not well-formed XML"

echo "# a type error in a hole points at the hole (plan A5)"
# Line 6 of the generated file is the literal; the hole's ( is column 41.
expect_reject '<a>${(+ x 1)}</a>' "tur:6:41: error"

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "# some cases failed"
  exit 1
fi
echo "# all $n cases passed"
