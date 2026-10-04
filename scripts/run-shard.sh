#!/usr/bin/env bash
# run-shard.sh -- run the per-spice CI stages for a list of spices, serially.
#
# Usage: scripts/run-shard.sh <tur> <spice>...
#
# For each spice, in spices/<name>: fetch native deps, type-check src/, run
# tests/, then errors/run.sh and fixtures/run.sh when present.  These are the
# same stages the per-spice matrix job ran as separate steps.  A failing
# spice does NOT stop the shard: every spice runs, each in its own log group,
# and the script exits non-zero at the end if any of them failed.  Under
# GitHub Actions, one row per spice is appended to $GITHUB_STEP_SUMMARY.
#
# Runs locally too: scripts/run-shard.sh ../turmeric/build/tur json crdt
set -u

if [ "$#" -lt 2 ]; then
  echo "usage: $0 <tur> <spice>..." >&2
  exit 2
fi
TUR_BIN="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
shift
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SUMMARY="${GITHUB_STEP_SUMMARY:-/dev/null}"
LOGDIR="$(mktemp -d)"

# tur is a Debug (ASan) build in CI; its frames are several times larger than
# a release build's, and emit_value_dispatch recurses over the expression
# tree.  ecs's corpus exhausts the default 8 MB stack.  Take the hard limit;
# macOS caps it well below `unlimited`.
ulimit -s unlimited 2>/dev/null || ulimit -s 65520 2>/dev/null || true

# Run tests under a virtual X server when one exists.  opengl's suites check
# for $DISPLAY and print "# SKIP: no display available" without asserting
# anything when it is missing -- so on a headless runner all 12 of its
# assertions were skipped while the job reported "3 tests, 3 passed".
# Spices that do not touch a display are unaffected.
TUR_TEST="$TUR_BIN"
if command -v xvfb-run >/dev/null 2>&1; then
  TUR_TEST="xvfb-run -a $TUR_BIN"
fi

annotate() {  # annotate <level> <spice> <message>
  echo "::$1 title=$2::$3"
}

# --- stages: each runs in spices/<name>, returns non-zero on failure -------

stage_fetch() {
  local spice="$1" log="$LOGDIR/$1.fetch.log" rc=0
  # Native libs come from :cmake-deps directly or transitively through
  # :spices (httpd, plot, ecs-raylib and ws-server declare none of their
  # own), so fetch whenever either section is present.  The compiler walks
  # the spice's declared :spices closure, not every workspace member
  # (turmeric-lang/turmeric#791); TUR_CMAKE_DEPS_WORKSPACE_WIDE=1 restores
  # the old workspace-wide walk if a spice ever needs it.
  if ! grep -qE ':cmake-deps|:spices' build.tur; then
    echo "no :cmake-deps or :spices declared"
    return 0
  fi
  "$TUR_BIN" fetch --update 2>&1 | tee "$log"
  rc=${PIPESTATUS[0]}
  if [ "$rc" -ne 0 ]; then
    # Non-fatal: an :optional :spices dep is expected to fail here.  Put the
    # tail of the output in the annotation so a required dep failing the same
    # way is findable without digging through the log.
    local tail
    tail=$(tail -n 20 "$log" | sed -e 's/%/%25/g' -e 's/\r/%0D/g' -e 's/$/%0A/' | tr -d '\n')
    annotate warning "$spice: tur fetch exited $rc" "$tail"
  fi
  # A native build that FAILED guarantees a missing archive later.  Die here,
  # where the compiler error is, not at an unrelated link error downstream.
  # Run 34448162505 is why: one mbedtls -Werror abort took out nine macOS
  # jobs, each reporting a *different* spice missing a *different* .a --
  # including libyyjson.a, which was merely next in the same aborted make.
  # Match the message, not $rc: rc also covers optional :spices failures,
  # which must stay non-fatal.
  if grep -q 'cmake build failed' "$log"; then
    annotate error "$spice: native dependency build failed" \
      "a :cmake-deps library failed to build; the compiler errors are in this spice's fetch log"
    grep -n 'cmake build failed' "$log"
    return 1
  fi
  return 0
}

stage_check() {
  local spice="$1" n_files=0 n_fail=0 f
  # Each `tur check` is a real signal for intra-spice import breakage and
  # missing exports now that per-file commands discover the enclosing spice.
  # requires.typecheck-skip marks a known-broken spice: report, do not fail.
  # The marker must say what is broken and point at a tracking issue; delete
  # it once the spice type-checks cleanly.
  if [ -f requires.typecheck-skip ]; then
    annotate warning "$spice: type-check skipped" "requires.typecheck-skip: $(head -n1 requires.typecheck-skip)"
    return 0
  fi
  [ -d src ] || { echo "no src/ directory; skipping check"; return 0; }
  while IFS= read -r -d '' f; do
    n_files=$((n_files + 1))
    if "$TUR_BIN" check "$f"; then
      echo "  ok   $f"
    else
      echo "::error file=spices/$spice/$f,title=$spice: tur check failed::$f"
      n_fail=$((n_fail + 1))
    fi
  done < <(find src -name '*.tur' -print0)
  echo "checked $n_files file(s), $n_fail failed"
  [ "$n_fail" -eq 0 ]
}

stage_test() {
  local spice="$1" n_fail=0 d
  [ -d tests ] || { echo "no tests/ directory; skipping test run"; return 0; }
  # `tur test <dir>` DOES recurse (a comment once claimed otherwise, which
  # is how ecs's compile-FAIL fixtures got run as tests and counted as 16
  # failures -- they live in errors/ now).  Two layouts exist:
  #   flat:   tests/*.tur, or a flat aggregator importing nested modules
  #           (linalg/tests/linalg.tur, template/tests/smoke.tur)
  #   nested: tests/<group>/*_test.tur, or tests/<group>/<case>/*.tur
  # Any flat .tur means one run over tests/ (descending would double-run what
  # an aggregator imports, and try to test template's non-suite fixtures).
  # Otherwise run each directory that directly holds a .tur file.
  if ls tests/*.tur >/dev/null 2>&1; then
    $TUR_TEST test tests
    return $?
  fi
  while IFS= read -r d; do
    echo "== tur test $d =="
    $TUR_TEST test "$d" || n_fail=$((n_fail + 1))
  done < <(find tests -name '*.tur' -print0 | xargs -0 -n1 dirname | sort -u)
  [ "$n_fail" -eq 0 ]
}

stage_errors() {
  # Compile-FAIL fixtures.  They cannot live under tests/ (`tur test`
  # recurses and would count each as a failure), and "does not compile"
  # alone lets a fixture drift onto an unrelated diagnostic -- so each
  # spice's errors/run.sh pins the code AND a witness substring.
  [ -x errors/run.sh ] || { echo "no errors/run.sh"; return 0; }
  errors/run.sh "$TUR_BIN"
}

stage_fixtures() {
  # Whole-program end-to-end fixtures (each with its own main), kept out of
  # tests/ for the same reason as errors/.  tourist, tourist-ws, tls,
  # ws-client and ws-server keep fixtures/ whose per-fixture scripts need a
  # live server or a Python helper; with no aggregate fixtures/run.sh they
  # are skipped here and stay manual.
  [ -x fixtures/run.sh ] || { echo "no fixtures/run.sh"; return 0; }
  fixtures/run.sh "$TUR_BIN"
}

# --- driver ---------------------------------------------------------------

{
  echo "| spice | result | failed stage | seconds |"
  echo "| --- | --- | --- | ---: |"
} >> "$SUMMARY"

failed=()
for spice in "$@"; do
  dir="$ROOT/spices/$spice"
  start=$(date +%s)
  result=pass
  failed_stage=""
  echo "::group::$spice"
  if [ ! -f "$dir/build.tur" ]; then
    annotate error "$spice" "no spices/$spice/build.tur"
    result=FAIL; failed_stage=missing
  else
    # A fatal fetch skips the rest of THIS spice only.  The other stages run
    # regardless, so one failure does not hide the next.
    for stage in fetch check test errors fixtures; do
      echo "--- $spice: $stage"
      if ! (cd "$dir" && "stage_$stage" "$spice"); then
        result=FAIL
        failed_stage="${failed_stage:+$failed_stage, }$stage"
        annotate error "$spice: $stage failed" "see the '$spice' log group"
        [ "$stage" = fetch ] && break
      fi
    done
  fi
  secs=$(( $(date +%s) - start ))
  echo "::endgroup::"
  echo "$spice: $result (${secs}s)${failed_stage:+ -- $failed_stage}"
  echo "| $spice | $result | ${failed_stage:--} | $secs |" >> "$SUMMARY"
  [ "$result" = pass ] || failed+=("$spice")
done

rm -rf "$LOGDIR"
if [ "${#failed[@]}" -gt 0 ]; then
  echo "${#failed[@]} of $# spice(s) failed: ${failed[*]}"
  exit 1
fi
echo "all $# spice(s) passed"
