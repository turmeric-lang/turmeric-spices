#!/usr/bin/env bash
# errors/run.sh -- assert the plot compile-fail diagnostics.
#
# These are compile-FAIL fixtures: each must be REJECTED, for a specific
# reason, so they live outside tests/ (`tur test tests` recurses and would
# count each as a suite failure). Each assertion pins a witness substring
# that only the intended defect produces: TUR-E0001 alone covers any type
# mismatch, so the code is too coarse to say the fixture still tests its
# subject.
#
# What they guard: a renderer is typed. Only a type with a `Renderer`
# instance boxes as an AnyRenderer, `plot` takes nothing else, a style must
# be the kind of style the renderer draws with, data must be the kind's
# record type, and a curve callback must have the kind's function type.
#
# scripts/run-shard.sh runs this as the spice's "errors" stage.
#
# Usage: errors/run.sh [path/to/tur]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SPICE_ROOT="$(cd "$HERE/.." && pwd)"
TUR="${1:-${TUR:-tur}}"

cd "$SPICE_ROOT"

fail=0
n=0

# expect_reject <file> <code> <witness> <desc>
#
# Asserts `tur check errors/<file>` reports <code> AND that the diagnostic
# text contains <witness>. Pass "-" as <code> for a diagnostic the compiler
# emits without a TUR-Exxxx tag; the witness is then the whole assertion.
expect_reject() {
  local file="$1" code="$2" witness="$3" desc="$4"
  local out
  n=$((n + 1))
  out="$("$TUR" check "errors/$file" 2>&1)"

  if [ "$code" = "-" ]; then
    if ! printf '%s' "$out" | grep -q 'error:'; then
      echo "not ok $n - $desc"
      echo "    expected an error, but the file compiled clean"
      fail=1
      return
    fi
  elif ! printf '%s' "$out" | grep -q "error \[$code\]"; then
    echo "not ok $n - $desc"
    if printf '%s' "$out" | grep -q "error \["; then
      echo "    expected $code; got:"
      printf '%s\n' "$out" | grep -m3 -o 'error \[TUR-[EW][0-9]*\]: .*' \
        | sed 's/^/      /'
    else
      echo "    expected $code, but the file compiled clean"
    fi
    fail=1
    return
  fi

  if ! printf '%s' "$out" | grep -qF "$witness"; then
    echo "not ok $n - $desc"
    echo "    the fixture was rejected, but not for the expected reason."
    echo "    expected the message to mention: $witness"
    echo "    got:"
    printf '%s\n' "$out" | grep -m3 -o 'error.*' | sed 's/^/      /'
    fail=1
    return
  fi

  echo "ok $n - $desc"
}

echo "# the plot compile-fail diagnostics"

expect_reject not-a-renderer.tur - \
  "pack: no instance 'Renderer' for type 'NotR'" \
  'any-renderer rejects a type with no Renderer instance'

expect_reject int-handle-vec.tur TUR-E0001 \
  "function 'plot' arg 1: expected (Vec (exists [a] [(Renderer a)] a)), got (Vec int)" \
  'plot rejects a (Vec int) of slot-bag handles'

expect_reject wrong-style-kind.tur TUR-E0001 \
  "function 'lines' arg 2: expected LineStyle, got FillStyle" \
  'lines rejects a FillStyle where its LineStyle is due'

expect_reject wrong-data-record.tur TUR-E0001 \
  "function 'lines' arg 1: expected (Vec Point), got (Vec Rect)" \
  'lines rejects rectangles where its points are due'

expect_reject wrong-callback-type.tur TUR-E0001 \
  "function 'function' arg 1: expected a function of type (fn [float] : float), got (fn [int] : int)" \
  'function rejects a callback of the wrong function type'

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# all $n compile-fail fixtures fired as expected"
