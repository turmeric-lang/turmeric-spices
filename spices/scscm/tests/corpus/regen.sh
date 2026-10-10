#!/usr/bin/env bash
# regen.sh -- rebuild the scscm compile-parity corpus.
#
# Usage: tests/corpus/regen.sh <hypercollider-checkout> [tur]
#
# For each program in SOURCES (paths in hypercollider, pinned in
# HYPERCOLLIDER_COMMIT):
#   <name>.scscm         a verbatim copy
#   <name>.lhc.scd       what hypercollider's lhc.js emits for it (reference
#                        only: most of it does not parse, see README.md)
#   <name>.expected.scd  what tur-scscm emits; corpus_test.tur compares
#                        against this byte for byte
#
# Review the .expected.scd diff before committing it, and run
# check-sclang.sh: the expected files are only worth keeping if they parse.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
hc="${1:?usage: regen.sh <hypercollider-checkout> [tur]}"
tur="${2:-${TUR_BIN:-tur}}"
pinned="$(cat "$here/HYPERCOLLIDER_COMMIT")"
actual="$(git -C "$hc" rev-parse HEAD)"
if [ "$actual" != "$pinned" ]; then
  echo "warning: $hc is at $actual, HYPERCOLLIDER_COMMIT pins $pinned" >&2
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# A one-file driver: compile argv[1] with compile-file, print the sclang.
cat > "$work/scscmc.tur" <<'TUR'
(defmodule scscmc
  (import scscm/compile :refer [compile-file])
  (import scscm/lexer :refer [lex-ok? lex-ok-val lex-err-val scscm-int-as-cstr])
  (defn put [s : cstr] #fx{Unsafe IO} : int
    ```c fputs((const char*)s, stdout); return 0; ```)
  (defn main [] #fx{Unsafe IO} : int
    (let [r (compile-file (scscm-int-as-cstr (head *args*)))]
      (if (lex-ok? r)
        (put (scscm-int-as-cstr (lex-ok-val r)))
        (do (put (scscm-int-as-cstr (lex-err-val r))) 1)))))
TUR
"$tur" build -I "$here/../../src" "$work/scscmc.tur" -o "$work/scscmc" >/dev/null 2>&1

while read -r name src _status; do
  case "$name" in ''|'#'*) continue ;; esac
  cp "$hc/$src" "$here/$name.scscm"
  (cd "$hc/cli" && node lhc.js "../$src" --to-sc) > "$here/$name.lhc.scd"
  "$work/scscmc" "$here/$name.scscm" > "$here/$name.expected.scd"
done < "$here/SOURCES"
echo "regenerated $(grep -c '^[a-z]' "$here/SOURCES") programs"
