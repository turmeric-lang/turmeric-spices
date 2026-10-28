#!/usr/bin/env bash
# errors/run.sh -- `#html{...}` holes in forbidden places are compile errors
# (xml-html-followups-plan follow-up A1).
#
# Each case is one literal, compiled through `html*` (the macro behind
# `#html{...}`) with `tur check`; it must be REJECTED with a message naming
# the place.  Pinning the witness keeps a case from drifting onto an
# unrelated diagnostic and passing without testing anything.  The
# generated files live in a temporary directory under the spice, so the
# spice's own src/ (and dom-core) resolve as they would for a user.
#
# The last case is a run-time one: a raw-html value that could not be
# written back (a <plaintext>) must panic rather than unbalance the literal.
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
# hole expressions refer to `x` (a cstr) and `b` (a bool).
expect_reject() {
  local lit="$1" witness="$2" f out
  n=$((n + 1))
  f="$TMP/case$n.tur"
  cat > "$f" <<EOF
(defmodule case$n
  (import html/markup :refer [html*])
  (import dom/holes :refer [html->string])
  (defn main [] : int
    (let [x "v" b true]
      (println (html->string (html* "$lit"))))
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

echo "# holes where a value could change the document's structure"
expect_reject '<${x}>' "a hole cannot be a tag name"
expect_reject '<p>a<${x}</p>' "a hole cannot be a tag name"
expect_reject '<p${x}>' "a hole cannot be part of a tag name"
expect_reject '</p${x}>' "a hole cannot be part of an end tag"
expect_reject '<p ${x}>' "a hole cannot be an attribute name"
expect_reject '<p data-${x}=1>' "a hole cannot be part of an attribute name"
expect_reject '<p title=a${x}>' "shares an unquoted attribute value"
expect_reject '<p title=${x}a>' "shares an unquoted attribute value"
expect_reject '<!-- ${x} -->' "a hole inside a comment"
expect_reject '<!doctype ${x}>' "a hole inside a doctype"
expect_reject '<? ${x} >' "a hole inside a doctype or markup declaration"
echo "# raw text"
expect_reject '<script>var a = ${x};</script>' "a hole inside <script>"
expect_reject '<style>p { color: ${x} }</style>' "a hole inside <style>"
expect_reject '<textarea>${x}</textarea>' "a hole inside <textarea>"
expect_reject '<title>${x}</title>' "a hole inside <title>"
expect_reject '<SCRIPT>${x}</SCRIPT>' "a hole inside <SCRIPT>"
echo "# attributes whose value is code or markup (plan A-4)"
expect_reject '<a onclick=\"go(${x})\">' "event-handler attribute 'onclick'"
expect_reject '<a ONMOUSEOVER=${x}>' "event-handler attribute 'ONMOUSEOVER'"
expect_reject '<svg onload=\"${x}\"></svg>' "event-handler attribute 'onload'"
expect_reject '<p style=\"color: ${x}\">' "a hole in the 'style' attribute"
expect_reject '<p STYLE=${x}>' "a hole in the 'STYLE' attribute"
expect_reject '<iframe srcdoc=\"${x}\"></iframe>' "a hole in the 'srcdoc' attribute"
echo "# documents and fragments (plan A-2)"
expect_reject '<head><title>t</title></head>' "a top-level <head> or <body>"
expect_reject '<body>${x}</body>' "a top-level <head> or <body>"
echo "# the literal itself"
expect_reject '<p>${}</p>' "an empty hole"
expect_reject '<p>${x</p>' 'unterminated ${'
expect_reject '<p>__tur_hole_0__</p>' "'__tur_hole_' is reserved"
echo "# types: the position picks the class"
expect_reject '<p title=${(html* \"<b>x</b>\")}>x</p>' "ToAttr"
expect_reject '<a href=${b}>x</a>' "ToUrlAttr"
expect_reject '<p>${b}</p>' "ToChild"

# A raw-html value that cannot be written back panics at render time.
n=$((n + 1))
f="$TMP/raw$n.tur"
cat > "$f" <<'EOF'
(defmodule rawcase
  (import html/markup :refer [html*])
  (import dom/holes :refer [html->string raw-html])
  (defn main [] : int
    (println (html->string (html* "<div>${(raw-html \"<plaintext>\")}</div><p>after</p>")))
    0))
EOF
out="$("$TUR" run "$f" 2>&1)"
if printf '%s' "$out" | grep -qF "cannot be written back as parsed"; then
  echo "ok $n - a <plaintext> raw-html value panics instead of unbalancing the literal"
else
  echo "not ok $n - a <plaintext> raw-html value panics instead of unbalancing the literal"
  printf '%s\n' "$out" | tail -5 | sed 's/^/      /'
  fail=1
fi

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "# some cases failed"
  exit 1
fi
echo "# all $n cases passed"
