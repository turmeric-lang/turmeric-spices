#!/usr/bin/env bash
# errors/run.sh -- assert the lmdb compile-fail diagnostics.
#
# These are compile-FAIL fixtures: each must be REJECTED, for a specific
# reason. That is the inverse of every other test in the spice, so they
# live outside tests/ -- `tur test tests` recurses, and counted each of
# these as a suite failure, which made this spice's CI job red on this
# directory alone while the real tests passed.
#
# Relocating them fixes the red job. This script is the other half: it
# asserts each fixture fails for the RIGHT reason. Checking only "does not
# compile" lets a fixture silently drift onto an unrelated diagnostic and
# stop testing its subject. So every assertion pins a witness substring
# that only the intended defect produces -- the code alone is too coarse,
# since TUR-E0001 / TUR-E0100 / TUR-E0101 each cover several fixtures here
# for unrelated reasons.
#
# ci.yml runs this via its "Assert compile-fail diagnostics" step, which is
# a no-op for any spice without an executable errors/run.sh.
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

echo "# the lmdb compile-fail diagnostics"

expect_reject lmdb-env-leak.tur TUR-E0100 \
  'linear value '\''env'\'' dropped without being consumed' \
  'an environment that is never closed is a leak'
expect_reject lmdb-txn-unfinished.tur TUR-E0100 \
  'linear value '\''txn'\'' dropped without being consumed' \
  'a transaction neither committed nor aborted is a leak'
expect_reject lmdb-cursor-double-close.tur TUR-E0101 \
  'linear value '\''c'\'' used after being consumed' \
  'closing a cursor twice is a use-after-consume'
expect_reject lmdb-txn-use-after-commit.tur TUR-E0101 \
  'linear value '\''txn'\'' used after being consumed' \
  'reading through a committed transaction is a use-after-consume'
expect_reject lmdb-scoped-callback-consumes.tur TUR-E0001 \
  '^linear and ^borrow parameters must match exactly' \
  'a scoped-transaction callback must borrow its Txn'

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# all $n compile-fail fixtures fired as expected"
