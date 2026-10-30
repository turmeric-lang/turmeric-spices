# tur-simd

Explicit SIMD for Turmeric, on GCC/Clang vector extensions, over the typed
numeric arrays (`F64Vec`, `F32Vec`, `U8Vec`, ... from `stdlib/numvec.tur`).

This is **phase S0** of turmeric's
[simd-and-vectorization-plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/simd-and-vectorization-plan.md):
before the compiler knew a vector type (phase S1), write
real kernels on the operation set S1 proposes, check that set against them,
and measure what a function boundary costs. Its findings are below, and in
the plan.

It now also carries **`simd/s1`**: the same kernels rewritten in ordinary
Turmeric on the vector types S1 gave the compiler (`F64x2`, `U8x16`,
`F32x4`, ... in `stdlib/simd.tur`) -- the plan's graduation test for S1.

Needs turmeric v0.73.0 or later, where the vector types need no flag (they
graduated from the `simd` experiment; v0.72.1 has them only behind
`--enable=simd`).

## What is in it

**`c/simd/tur_simd.h`** -- the proposed S1 operation set, one C function per
operation, named as S1 would spell it:

| | |
|---|---|
| types | `f64x2` `f64x4` `f32x4` `i32x4` `u8x16` `u16x8` |
| memory | `load` `store` (from / to an array's data at an index) |
| construction | `splat`, `lane` |
| lanewise | `add` `sub` `mul` `div` `min` `max`, `shr` (u16) |
| widen / narrow | `u8x16_widen_lo` / `_hi` -> `u16x8`, `u16x8_narrow` |
| horizontal | `hsum` (fixed tree), `hsum2` (N1's eight-lane tree) |

Two lowerings behind the same names, which is S1's preamble plan: GNU vector
extensions under gcc and clang, and a struct of lanes with a loop per
operation under MIR's c2mir (`tur jit`). Every operation does its lanes'
arithmetic in the same order on both, multiplies and adds never fused, so the
two agree bit for bit -- checked on gcc, clang, gcc forced onto the lanes
path, and real c2mir.

**`simd/kernels`** -- the three kernels the plan names, each a whole loop in
one inline-C body (a 16- or 32-byte vector is not a Turmeric value yet):

```turmeric
(import simd/kernels :refer [Biquad simd-dot-f64 simd-blend-u8! simd-biquad4-f32!])

(simd-dot-f64 a b)                     ; = f64vec-dot, to the bit
(simd-blend-u8! out fg bg (as uint8 96))
(simd-biquad4-f32! samples state (Biquad b0 b1 b2 a1 a2))   ; 4 interleaved channels
```

| function | |
|---|---|
| `simd-dot-f64`, `simd-dot-f32` | dot product on four-lane accumulators in N1's eight-lane order: bit-identical to `f64vec-dot` / `f32vec-dot` |
| `simd-dot-f64x4` | the same on two `f64x4` accumulators, kept for the measurement below |
| `simd-blend-u8!` | `dst = (a*alpha + b*(255-alpha)) / 255`, rounded, sixteen bytes a step through 16-bit lanes |
| `simd-biquad4-f32!` | one biquad (DF2T) over four interleaved channels, a channel per lane; state persists in an `F32Vec` of 8 |
| `simd-lowering` | `"vector-ext"` or `"lanes"` |

**`simd/s1`** -- the dot product, blend and biquad again, written in
Turmeric on the S1 vector types: `let`, `while`, and `{a + b}` on vectors
through their `Num` instances, with no inline C:

```turmeric
(import simd/s1 :refer [s1-dot-f64 s1-blend-u8! s1-biquad4-f32!])

(s1-dot-f64 a b)                       ; = f64vec-dot = simd-dot-f64, to the bit
(s1-blend-u8! out fg bg (as uint8 96))
(s1-biquad4-f32! samples state (Biquad b0 b1 b2 a1 a2))
```

The inner loops read with `P-load-unchecked` after checking the lengths
once; a checked `P-load` carries a panic check per call, which costs a call
under clang.

`tests/simd.tur` holds every kernel, inline-C and S1, to its scalar result
bit for bit (21 tests).

## S1 against inline C (2026-10-30)

`bench/bench.tur`, ns/elem, best of five, x86-64, `-O2`:

| kernel | gcc: inline C | gcc: S1 | clang: inline C | clang: S1 |
|---|---|---|---|---|
| dot f64 (`F64x2`) | 0.242 | 0.249 | 0.251 | 0.237 |
| blend u8 (`U8x16` / `U16x8`) | 0.299 | 0.299 | 0.146 | 0.151 |
| biquad x4 f32 (`F32x4`) | 0.969 | 0.985 | 0.974 | 1.32 |

The dot product and the blend are as fast in Turmeric as in hand-written
vector C on both compilers, which is the plan's graduation test for S1. The
biquad is too on gcc, and 1.35x slower on clang: its loop carries two
vectors (the filter state) from one iteration to the next, and clang keeps a
16-byte struct of four floats as the two eight-byte halves the x86-64 ABI
passes it in, so each iteration splits the state (`unpckhpd`) and joins it
again (`movlhps`) on the recurrence's critical path. The dot product's four
accumulators are independent, so the same shuffles hide behind them. A union
with a vector member does not change the ABI's classification; the fix would
be in how the compiler declares the vector types.

## Findings (2026-10-29)

`bench/bench.tur`, x86-64 (Xeon, 2.1 GHz, SSE2 baseline unless noted), ns
per element, best of five:

| | gcc -O2 | clang -O2 | gcc -O2 -mavx2 |
|---|---|---|---|
| dot: `simd-dot-f64` (f64x2) | 0.22 | 0.22 | 0.24 |
| dot: `simd-dot-f64x4` | **0.60** | 0.22 | 0.14 |
| dot: `f64vec-dot` (N1, auto-vectorized) | 0.22 | 0.22 | 0.13 |
| dot: scalar Turmeric loop | 0.67 | 0.66 | 0.67 |
| blend: `simd-blend-u8!` | 0.29 | 0.15 | |
| blend: scalar C, auto-vectorized | 0.66 | 1.03 | |
| blend: scalar Turmeric loop | 1.06 | 1.03 | |
| biquad x4: `simd-biquad4-f32!` | 0.88 | 0.92 | |
| biquad x4: scalar C, channels side by side | 0.89 | 0.87 | |
| biquad x4: 4 scalar Turmeric filters | 7.05 | 10.6 | |

1. **A vector type wider than the machine's registers is slower than none
   on gcc.** Without AVX, gcc keeps an `f64x4` in memory: the dot's two
   accumulators are stored and reloaded every iteration, 2.7x slower than
   N1. clang splits it into registers. The 16-byte `f64x2` matches N1
   everywhere. So S1's portable types are the 16-byte ones; a 32-byte type
   pays off only under `-mavx`.
2. **Every operation must inline.** Built with `-DTUR_SIMD_OUTOFLINE` (each
   operation a real call, vectors by value), the kernels run 3-7x slower
   than inlined -- slower than the scalar Turmeric loops (f64x4 dot 1.86, blend
   1.99, biquad 3.17 ns/elem on gcc). A 32-byte vector also crosses a call in
   memory without AVX (gcc's `-Wpsabi`). S1 must emit its operations as C
   operators or always-inlined helpers, never as Turmeric function calls.
3. **Explicit lanes win where C's promotions get in the way.** The blend
   needs 16-bit lanes; C promotes `uint8_t` arithmetic to 32 bits, so the
   auto-vectorizer runs at half width (gcc) or not at all (clang). The
   explicit kernel is 2.3x / 7x faster than the same scalar C.
4. **For a dot product, the auto-vectorized N1 kernel is already as fast.**
   Explicit SIMD buys nothing there.
5. **For a recurrence, the lanes are the point, not the instructions.** The
   biquad's win (8-12x over four scalar Turmeric filters) comes from running
   four channels side by side with the state in registers; scalar C written
   that way matches the explicit kernel. Turmeric has no way to write that
   shape today; S1's vector types would be it.
6. **The struct-of-lanes lowering is correct but slow on gcc** (dot 3.8,
   blend 1.2 ns/elem with `-D__MIRC__`): fine for `tur jit`'s
   run-edit-run loop, not for anything measured.
7. **Op set**: the three kernels needed `load`, `store`, `splat`, `lane`,
   lanewise `add`/`sub`/`mul`, `shr`, widen/narrow and a fixed-order
   horizontal sum. Nothing needed `shuffle` or masks; `min`/`max`/`div` are
   in the header unused by a kernel.

Two toolchain issues found on the way: c2mir lexed identifiers starting `u8`
as `88...`, so the header never pastes a type name (already fixed on turmeric
`main`: `docs/archive/c2mir-paste-of-u8-prefixed-token.md`); and `tur jit`
does not pass a spice's `:c-includes` to c2mir, so these tests fall back to cc
under it (`docs/reported/jit-ignores-spice-c-includes.md`).

## Running

```sh
tur test tests                     # from spices/simd
tur run bench/bench.tur
CC=clang tur run bench/bench.tur
TUR_CC_FLAGS="-O2 -std=c99 -fno-strict-aliasing -w -DTUR_SIMD_OUTOFLINE" tur run bench/bench.tur
TUR_CC_FLAGS="-O2 -std=c99 -fno-strict-aliasing -w -D__MIRC__" tur test tests   # the lanes lowering
```
