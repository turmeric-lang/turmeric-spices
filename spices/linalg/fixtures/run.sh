#!/usr/bin/env bash
# fixtures/run.sh -- the linalg BLAS/LAPACK backend (linalg-spice-followups
# plan, Part C).
#
# The backend is compiled in with -DTUR_LINALG_BLAS and a BLAS/LAPACK on the
# link line, both of which a consuming project sets in its own build.tur:
#
#   :build-opts #map{:c-flags   ["-DTUR_LINALG_BLAS=1"]
#                    :link-libs ["openblas"]}                 ; Linux
#   :build-opts #map{:c-flags    ["-DTUR_LINALG_BLAS=1"]
#                    :link-flags ["-framework Accelerate"]}   ; macOS
#
# This script builds exactly such a project in .blas-work/ (gitignored) and
#
#   1. runs every linalg test in ../tests UNMODIFIED against the BLAS build;
#   2. runs blas-tests/agree.tur, which computes each routed operation through
#      BLAS/LAPACK and through the pure kernels in one process and requires
#      agreement to 12 significant figures;
#   3. times tests/mul.tur's 512x512 multiply both ways and requires BLAS to
#      be at least 2x faster than the tiled pure path.
#
# With no BLAS on the machine it skips -- except under CI (CI=true), where a
# silent skip would quietly drop the coverage, so it fails instead.
#
# scripts/run-shard.sh runs this as the spice's fixtures stage.
#
# Usage: fixtures/run.sh [path/to/tur]
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
SPICE="$(cd "$HERE/.." && pwd)"
TUR="${1:-${TUR:-tur}}"
case "$TUR" in */*) TUR="$(cd "$(dirname "$TUR")" && pwd)/$(basename "$TUR")" ;; esac

echo "# linalg BLAS/LAPACK backend"

link=""
case "$(uname -s)" in
  Darwin)
    link=':link-flags ["-framework Accelerate"]'
    ;;
  *)
    probe="$(mktemp -d)"
    printf 'extern void cblas_dgemm(void);\nextern void dsyev_(void);\nint main(void){cblas_dgemm();dsyev_();return 0;}\n' > "$probe/p.c"
    if ${CC:-cc} "$probe/p.c" -o "$probe/p" -lopenblas >/dev/null 2>&1; then
      link=':link-libs ["openblas"]'
    fi
    rm -rf "$probe"
    ;;
esac

if [ -z "$link" ]; then
  if [ -n "${CI:-}" ]; then
    echo "not ok 1 - no BLAS/LAPACK found (install libopenblas-dev); under CI that is a failure, not a skip"
    exit 1
  fi
  echo "# SKIP: no BLAS/LAPACK found (Linux: install libopenblas-dev)"
  exit 0
fi

WORK="$HERE/.blas-work"
rm -rf "$WORK"
mkdir -p "$WORK/tests"
cat > "$WORK/build.tur" <<MANIFEST
(defpackage linalg-blas-check
  :name "linalg-blas-check"
  :version "0.0.0"
  :spices #map{"linalg" #map{:path "../.."}}
  :build-opts #map{:c-flags ["-DTUR_LINALG_BLAS=1"] $link})
MANIFEST
cp "$SPICE"/tests/*.tur "$WORK/tests/"
cp "$HERE"/blas-tests/*.tur "$WORK/tests/"

fail=0
cd "$WORK" || exit 1

echo "# 1-2: the linalg suite and the BLAS/pure agreement checks, BLAS build"
if ! "$TUR" test tests; then
  echo "not ok - linalg tests under the BLAS build"
  fail=1
fi

echo "# 3: 512x512 mat-mul, BLAS vs the tiled pure path"
ms() {  # ms <log> -- the "# mat-mul 512x512: N ms" figure
  sed -n 's/^# mat-mul 512x512: \([0-9.]*\) ms$/\1/p' "$1" | head -n1
}
"$TUR" run tests/mul.tur > blas.log 2>&1
TUR_LINALG_NO_BLAS=1 "$TUR" run tests/mul.tur > pure.log 2>&1
b="$(ms blas.log)"
p="$(ms pure.log)"
if [ -z "$b" ] || [ -z "$p" ]; then
  echo "not ok - could not read the timings"
  cat blas.log pure.log
  fail=1
elif awk -v b="$b" -v p="$p" 'BEGIN { exit !(p >= 2 * b) }'; then
  echo "ok - BLAS $b ms vs pure $p ms (>= 2x)"
else
  echo "not ok - BLAS $b ms vs pure $p ms: less than the 2x the plan requires"
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "FAILED"
  exit 1
fi
echo "# linalg BLAS backend: all checks passed"
