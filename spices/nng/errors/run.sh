#!/usr/bin/env bash
# errors/run.sh -- assert the nng compile-fail diagnostics.
#
# These are compile-FAIL fixtures: each must be REJECTED, for a specific
# reason. That is the inverse of every other test in the spice, so they live
# outside tests/ -- `tur test tests` recurses, and would count each of these as
# a suite failure.
#
# This script is the other half: it asserts each fixture fails for the RIGHT
# reason. Checking only "does not compile" lets a fixture silently drift onto
# an unrelated diagnostic and stop testing its subject. So every assertion pins
# a witness substring that only the intended defect produces -- the code alone
# is too coarse, since TUR-E0101 covers two fixtures here for different
# reasons (a second consume vs. a borrow after the consume).
#
# ci.yml runs this via its "Assert compile-fail diagnostics" step, which is a
# no-op for any spice without an executable errors/run.sh.
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
# Asserts `tur check errors/<file>` reports <code> AND that the diagnostic text
# contains <witness>. Pass "-" as <code> for a diagnostic the compiler emits
# without a TUR-Exxxx tag; the witness is then the whole assertion.
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

echo "# the nng compile-fail diagnostics"

expect_reject nng-double-close.tur TUR-E0101 \
  'linear value '\''s'\'' used after being consumed' \
  'closing a socket twice is a use-after-consume'
expect_reject nng-use-after-close.tur TUR-E0101 \
  'linear value '\''s'\'' used after being consumed' \
  'listening on a closed socket is a use-after-consume'
expect_reject nng-leak-no-close.tur TUR-E0100 \
  'linear value '\''s'\'' dropped without being consumed' \
  'a socket that is never closed is a leak'
expect_reject nng-aio-double-free.tur TUR-E0101 \
  'linear value '\''a'\'' used after being consumed' \
  'freeing an Aio twice is a use-after-consume'
expect_reject nng-aio-use-after-free.tur TUR-E0101 \
  'linear value '\''a'\'' used after being consumed' \
  'submitting on a freed Aio is a use-after-consume'
expect_reject nng-aio-leak-no-free.tur TUR-E0100 \
  'linear value '\''a'\'' dropped without being consumed' \
  'an Aio that is never freed is a leak'
expect_reject nng-ctx-double-close.tur TUR-E0101 \
  'linear value '\''c'\'' used after being consumed' \
  'closing a context twice is a use-after-consume'
expect_reject nng-ctx-use-after-close.tur TUR-E0101 \
  'linear value '\''c'\'' used after being consumed' \
  'receiving on a closed context is a use-after-consume'
expect_reject nng-ctx-leak-no-close.tur TUR-E0100 \
  'linear value '\''c'\'' dropped without being consumed' \
  'a context that is never closed is a leak'

echo "1..$n"
if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# all $n compile-fail fixtures fired as expected"
