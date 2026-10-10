#!/usr/bin/env bash
# render.sh -- render the corpus's sound-making programs and check the audio
# (scscm-host-testing-plan T2/T3).
#
# Usage: tests/host/render.sh [--host scsynth|hcsynth|all] [--keep DIR]
#
# Each program in PROGRAMS is rendered from tests/corpus/<name>.expected.scd
# (the sclang tur-scscm emits) on every host present:
#
#   scsynth  stock SuperCollider: capture.scd runs the program in sclang
#            against a recording stand-in server for <seconds> of real time,
#            and scsynth -N renders the score. Needs sclang and scsynth on
#            PATH (or $SCLANG / $SCSYNTH).
#   hcsynth  hypercollider: hclang captures the OSC, hcsynth renders it.
#            Needs $HC_DIR, a hypercollider checkout with build/wasm built
#            (see HOSTS), and node. Its capture has no clock, so only
#            programs marked "both" run here.
#
# The checks in PROGRAMS run on every render; when a program is rendered on
# both hosts the two WAVs must also agree (wavcheck.py --compare). A host
# that is not installed prints SKIP and is not a failure. Exit status is the
# number of failures.
set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
corpus="$here/../corpus"
host=all
keep=""
while [ $# -gt 0 ]; do
  case "$1" in
    --host) host="$2"; shift 2 ;;
    --keep) keep="$2"; shift 2 ;;
    *) echo "usage: $0 [--host scsynth|hcsynth|all] [--keep DIR]" >&2; exit 2 ;;
  esac
done

work="${keep:-$(mktemp -d)}"
mkdir -p "$work"
[ -n "$keep" ] || trap 'rm -rf "$work"' EXIT

sclang="${SCLANG:-$(command -v sclang || true)}"
scsynth="${SCSYNTH:-$(command -v scsynth || true)}"
have_sc=0; [ -n "$sclang" ] && [ -n "$scsynth" ] && have_sc=1
have_hc=0
if [ -n "${HC_DIR:-}" ] && [ -f "$HC_DIR/build/wasm/lang/hclang/hclang.js" ] \
   && [ -f "$HC_DIR/build/wasm/server/hcsynth/hcsynth.js" ] && command -v node >/dev/null; then
  have_hc=1
fi
case "$host" in
  scsynth) have_hc=0 ;;
  hcsynth) have_sc=0 ;;
esac
[ "$have_sc" = 1 ] || echo "SKIP: no scsynth (needs sclang and scsynth)"
[ "$have_hc" = 1 ] || echo "SKIP: no hcsynth (set HC_DIR to a built hypercollider checkout)"

fail=0
note() { echo "$1"; case "$1" in FAIL*) fail=$((fail + 1)) ;; esac; }

render_sc() {  # name seconds -> $work/name-scsynth.wav
  # Headless: no display, and QtWebEngine refuses to start as root without this.
  QT_QPA_PLATFORM=offscreen QTWEBENGINE_DISABLE_SANDBOX=1 \
    timeout $(( ${2%.*} + 60 )) "$sclang" "$here/capture.scd" \
      "$corpus/$1.expected.scd" "$work/$1.osc" "$2" > "$work/$1-sclang.log" 2>&1 \
  && "$scsynth" -N "$work/$1.osc" _ "$work/$1-scsynth.wav" 48000 WAV int16 -o 2 \
       > "$work/$1-scsynth.log" 2>&1
}

render_hc() {  # name seconds -> $work/name-hcsynth.wav
  # hclang resolves build/wasm from the working directory. A 64-sample block
  # matches scsynth's control rate (hcsynth's default is 512).
  (cd "$HC_DIR" \
   && timeout 120 node cli/hclang.js --no-zeroconf --script "$corpus/$1.expected.scd" \
        --output "$work/$1.json" > "$work/$1-hclang.log" 2>&1 \
   && timeout 120 node cli/hcsynth.js --commands "$work/$1.json" \
        --output "$work/$1-hcsynth.wav" --duration "$2" --block-size 64 \
        > "$work/$1-hcsynth.log" 2>&1)
}

while read -r name seconds hosts checks; do
  case "$name" in ''|'#'*) continue ;; esac
  rendered=()
  if [ "$have_sc" = 1 ]; then
    if render_sc "$name" "$seconds"; then
      rendered+=(scsynth)
    else
      note "FAIL $name scsynth: render failed (logs: $work/$name-*.log)"
    fi
  fi
  if [ "$have_hc" = 1 ] && [ "$hosts" = both ]; then
    if render_hc "$name" "$seconds"; then
      rendered+=(hcsynth)
    else
      note "FAIL $name hcsynth: render failed (logs: $work/$name-*.log)"
    fi
  fi
  for h in "${rendered[@]}"; do
    # shellcheck disable=SC2086  # checks is a word list
    out="$(python3 "$here/wavcheck.py" "$work/$name-$h.wav" $checks)"
    st=$?
    echo "$out" | sed "s/^\(ok\|FAIL\) *\(.*\)$/\1 $name $h: \2/"
    fail=$((fail + st))
  done
  if [ "${#rendered[@]}" = 2 ]; then
    end="$(python3 -c "print(float('$seconds') - 0.1)")"
    out="$(python3 "$here/wavcheck.py" --compare "$work/$name-scsynth.wav" "$work/$name-hcsynth.wav" 0 "$end")"
    st=$?
    echo "$out" | sed "s/^\(ok\|FAIL\) *\(.*\)$/\1 $name scsynth~hcsynth: \2/"
    fail=$((fail + st))
  fi
done < "$here/PROGRAMS"

[ "$fail" = 0 ] && echo "all checks passed" || echo "$fail failed"
exit "$fail"
