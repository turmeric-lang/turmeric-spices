#!/usr/bin/env bash
# errors/run.sh -- `#html{...}` holes in forbidden places, and `hiccup`
# literals the plan rules out, are compile errors (xml-html-followups-plan
# follow-ups A1 and D1).
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

# ---- hiccup (follow-up D1) ------------------------------------------------------
#
# expect_hiccup_reject <form> <witness>: `(hiccup <form>)` must not compile.
# The form is Turmeric source; it may use `x` (a cstr) and `k` (an int).
expect_hiccup_reject() {
  local form="$1" witness="$2" f out
  n=$((n + 1))
  f="$TMP/hiccup$n.tur"
  cat > "$f" <<EOF
(defmodule hcase$n
  (import html/hiccup :refer [hiccup hiccup->string])
  (defn main [] : int
    (let [x "v" k 3]
      (println (hiccup->string (hiccup $form))))
    0))
EOF
  out="$("$TUR" check "$f" 2>&1)"
  if printf '%s' "$out" | grep -qF -- "$witness"; then
    echo "ok $n - hiccup $form"
  else
    echo "not ok $n - hiccup $form"
    echo "    expected a diagnostic containing: $witness"
    printf '%s\n' "$out" | grep -m3 'error' | sed 's/^/      /'
    fail=1
  fi
}

echo "# hiccup: names and shapes"
expect_hiccup_reject '[:p+q "x"]' "is not a valid element name"
expect_hiccup_reject '["1p" "x"]' "is not a valid element name"
expect_hiccup_reject '[:p #map{"a b" "1"} "x"]' "is not a valid attribute name"
expect_hiccup_reject '[x "y"]' "a computed tag is not allowed"
expect_hiccup_reject '[:div [x "y"]]' "a computed tag is not allowed"
expect_hiccup_reject '"text"' "expected an element vector"
expect_hiccup_reject '[:br "x"]' "is a void element"
expect_hiccup_reject '[:img [:b]]' "is a void element"
expect_hiccup_reject '[:p#a#b "x"]' "a second id in the tag shorthand"
expect_hiccup_reject '[:p#a #map{:id "b"} "x"]' "a second id"
expect_hiccup_reject '[:p {:class "a"} "x"]' "write the attributes as #map{...}"
expect_hiccup_reject '[:p {:class "a" :id "b"} "x"]' "write the attributes as #map{...}"
expect_hiccup_reject '[:p #map{1 "a"} "x"]' "an attribute key must be a keyword or a string"
expect_hiccup_reject '[:plaintext "x"]' "<plaintext> never closes"
echo "# hiccup: code-bearing attributes and raw text (plan A-4)"
expect_hiccup_reject '[:a #map{:onclick x} "y"]' "a dynamic value for 'onclick'"
expect_hiccup_reject '[:p #map{:style x} "y"]' "a dynamic value for 'style'"
expect_hiccup_reject '[:iframe #map{:srcdoc x}]' "a dynamic value for 'srcdoc'"
expect_hiccup_reject '[:script x]' "a dynamic <script> body"
expect_hiccup_reject '[:style x]' "a dynamic <style> body"
expect_hiccup_reject '[:script "a" "b"]' "takes one string literal"
expect_hiccup_reject '[:script "x</script><b>"]' "would end the element early"
expect_hiccup_reject '[:script "<!-- x"]' "would end the element early"
expect_hiccup_reject '[:style "a</STYLE>"]' "would end the element early"
echo "# hiccup: nesting the HTML parser would undo (plan D-2)"
expect_hiccup_reject '[:p [:div "x"]]' "<div> inside <p>"
expect_hiccup_reject '[:p [:ul [:li "x"]]]' "<ul> inside <p>"
expect_hiccup_reject '[:a [:span [:a "x"]]]' "<a> inside <a>"
expect_hiccup_reject '[:form [:div [:form]]]' "<form> inside <form>"
expect_hiccup_reject '[:a [:button "x"]]' "interactive <button> inside <a> or <button>"
expect_hiccup_reject '[:button [:input]]' "interactive <input> inside <a> or <button>"
expect_hiccup_reject '[:table [:td "x"]]' "<td> directly in <table>"
expect_hiccup_reject '[:table "x"]' "text directly in <table>"
expect_hiccup_reject '[:tbody [:td "x"]]' "<td> directly in <tbody>"
expect_hiccup_reject '[:tr [:div "x"]]' "<div> directly in <tr>"
echo "# hiccup: types, through the same classes as #html{}"
expect_hiccup_reject '[:p #map{:title (hiccup [:b])} "x"]' "ToAttr"
expect_hiccup_reject '[:a #map{:href true} "x"]' "ToUrlAttr"
expect_hiccup_reject '[:p true]' "ToHtml"
echo "# hiccup: result positions (plan D1b)"
expect_hiccup_reject '[:ul (let [v [:li x]] v)]' "wrap it in (hiccup ...)"
expect_hiccup_reject '[:ul (str-concat [:li x])]' "wrap it in (hiccup ...)"
expect_hiccup_reject '[:ul (when true [:li x] "y")]' "wrap it in (hiccup ...)"
expect_hiccup_reject '[:ul (if [:b] "a" "b")]' "wrap it in (hiccup ...)"
expect_hiccup_reject '[:table (if true [:td "x"])]' "<td> directly in <table>"
expect_hiccup_reject '[:p (cond (> k 1) [:div "x"])]' "<div> inside <p>"
expect_hiccup_reject '[:a (match (Some k) (Some v) [:a v] (None) "")]' "<a> inside <a>"
expect_hiccup_reject '[:tr (do "x")]' "text directly in <tr>"
expect_hiccup_reject '[:ul (if true [x])]' "a computed tag is not allowed"
# A type error inside a rewritten branch points at the user's line and
# column (the `(+ x 1)` in the literal), not at the expansion.
expect_hiccup_reject '[:div (if true [:b (+ x 1)] [:i])]' "tur:5:59: error"

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
