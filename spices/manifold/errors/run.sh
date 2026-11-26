#!/usr/bin/env bash
# errors/run.sh -- assert the manifold compile-fail diagnostics (linear handle discipline).
#
# These are compile-FAIL fixtures: each must be REJECTED, for a specific
# reason.  That is the inverse of every other test in the spice, so they live
# outside tests/ -- `tur test tests` recurses and would count each one as a
# suite failure.
#
# "Does not compile" alone lets a fixture drift onto an unrelated diagnostic
# and stop testing its subject, so every assertion pins the code AND a witness
# substring naming the binding at fault.
#
# What is NOT here: the three shapes the checker accepts and that leak at run
# time (README "What the checker does not catch").  A fixture pinning "this
# still compiles" would turn this spice's CI red the day turmeric closes one
# of them.  tests/ run under LeakSanitizer in CI, which is what catches those.
#
# scripts/run-shard.sh runs this for any spice with an executable
# errors/run.sh.
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

echo "# the manifold compile-fail diagnostics (linear handle discipline)"

expect_reject manifold-solid-leak.tur TUR-E0100 \
  'linear value '\''c'\'' dropped without being consumed' \
  'a Solid that is bound and only queried is a leak'
expect_reject manifold-solid-use-after-free.tur TUR-E0101 \
  'linear value '\''c'\'' used after being consumed' \
  'querying a freed Solid is a use-after-consume'
expect_reject manifold-solid-reused-after-op.tur TUR-E0101 \
  'linear value '\''a'\'' used after being consumed' \
  'an operand is gone once an operation has taken it'
expect_reject manifold-solid-same-operand-twice.tur TUR-E0101 \
  'linear value '\''a'\'' used after being consumed' \
  'one handle cannot be both operands of a boolean'
expect_reject manifold-solid-one-branch.tur TUR-E0104 \
  'linear value '\''a'\'' consumed in one branch but not the other' \
  'consuming a Solid in one if-branch only is rejected'
expect_reject manifold-borrowed-solid-consumed.tur TUR-E0109 \
  'borrowed value '\''s'\'' cannot be consumed here' \
  'a ^borrow Solid cannot be freed by the function borrowing it'
expect_reject manifold-solid-vec-leak.tur TUR-E0100 \
  'linear value '\''parts'\'' dropped without being consumed' \
  'a SolidVec that is filled and never consumed is a leak'

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# all $n compile-fail fixtures fired as expected"
