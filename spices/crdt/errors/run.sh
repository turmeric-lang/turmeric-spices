#!/usr/bin/env bash
# errors/run.sh -- assert the crdt compile-fail diagnostics.
#
# These are compile-FAIL fixtures: each must be REJECTED, for a specific
# reason, so they live outside tests/ (`tur test tests` recurses and would
# count each as a suite failure). Each assertion pins a witness substring
# that only the intended defect produces: TUR-E0001 alone covers any type
# mismatch, so the code is too coarse to say the fixture still tests its
# subject.
#
# What they guard: a replica is a ReplicaId, a newtype over Sym, so the
# element or key symbols the same calls take cannot stand in for it.
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

echo "# the crdt compile-fail diagnostics"

expect_reject element-as-replica.tur TUR-E0001 \
  "function 'orset-add' arg 3: expected ReplicaId, got :Sym" \
  'orset-add rejects a bare Sym where the replica is due'

expect_reject key-as-replica.tur TUR-E0001 \
  "function 'ormap-put' arg 2: expected ReplicaId, got :Sym" \
  'ormap-put rejects its key in the replica position'

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# all $n compile-fail fixtures fired as expected"
