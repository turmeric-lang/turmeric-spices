/* tur_simd.h -- tur-simd's explicit-SIMD operations (simd-and-vectorization-
 * plan phase S0, turmeric repo docs/upcoming/simd-and-vectorization-plan.md).
 *
 * The op set S1 proposes as compiler-known vector types, written in C so the
 * spice's kernels can be built from it before the compiler knows any of it.
 * Each operation is one function, named as S1 would spell it, over one of
 * five vector types:
 *
 *   f64x2  2 x double     f64x4  4 x double     f32x4  4 x float
 *   i32x4  4 x int32_t    u8x16 16 x uint8_t    u16x8  8 x uint16_t
 *
 * Two lowerings behind the same names -- the split S1 plans for its
 * preambles:
 *
 *   - a C compiler with GNU vector extensions (gcc, clang): `vector_size`
 *     types, so `a + b` is one SSE / AVX / NEON / simd128 instruction (an
 *     f64x4 on an SSE2-only target is two 128-bit operations);
 *   - MIR's c2mir (`tur jit`, `__MIRC__`), which has no vector types: a struct
 *     of lanes and a loop per operation.  Same results, scalar speed.
 *
 * Every operation does its lanes' arithmetic in the same order on both, and
 * no multiply is fused into an add (each is its own statement), so the two
 * lowerings agree bit for bit.  `simd_f64x4_hsum2` adds two accumulators in
 * N1's fixed eight-lane tree, so a dot product built from it is bit-identical
 * to `f64vec-dot`.
 *
 * TUR_SIMD_OUTOFLINE (a -D flag) makes every operation a real, non-inlined
 * call taking and returning its vectors by value: what each Turmeric-level
 * operation would cost if S1 could not inline it.  bench/ measures it.
 */
#ifndef TUR_SIMD_H
#define TUR_SIMD_H

#include <stdint.h>
#include <string.h>

#if defined(TUR_SIMD_OUTOFLINE) && defined(__GNUC__)
#define TS_OP static __attribute__((noinline, unused))
#else
#define TS_OP static inline
#endif

#if (defined(__GNUC__) || defined(__clang__)) && !defined(__MIRC__)
#define TUR_SIMD_VECTOR_EXT 1
/* "AVX vector argument without AVX enabled changes the ABI": a 32-byte f64x4
 * crossing a call on an SSE2-only build travels through memory, not a
 * register.  True, and the point of TUR_SIMD_OUTOFLINE's measurement; not a
 * defect in the program, so not a warning on every call.  (A finding for S1:
 * a 32-byte vector type needs AVX to stay in registers across a call.) */
#pragma GCC diagnostic ignored "-Wpsabi"
typedef double   f64x2 __attribute__((vector_size(16)));
typedef double   f64x4 __attribute__((vector_size(32)));
typedef float    f32x4 __attribute__((vector_size(16)));
typedef int32_t  i32x4 __attribute__((vector_size(16)));
typedef uint8_t  u8x16 __attribute__((vector_size(16)));
typedef uint16_t u16x8 __attribute__((vector_size(16)));
#else
#define TUR_SIMD_VECTOR_EXT 0
typedef struct { double   v[2]; }  f64x2;
typedef struct { double   v[4]; }  f64x4;
typedef struct { float    v[4]; }  f32x4;
typedef struct { int32_t  v[4]; }  i32x4;
typedef struct { uint8_t  v[16]; } u8x16;
typedef struct { uint16_t v[8]; }  u16x8;
#endif

/* Every generator macro below takes the functions' whole name prefix (`F`,
 * e.g. simd_u8x16) rather than pasting it together from the type name:
 * c2mir's preprocessor mis-lexes a `##` operand that starts with `u8` (a
 * u8"..." string prefix to it) -- `u8 ## x` comes out as the number 88x, and
 * simd_##u8x16##_load as an undeclared name (turmeric
 * docs/reported/c2mir-paste-of-u8-prefixed-token.md). */

/* The lanewise binary operations, for one type.  `L` is its lane count. */
#if TUR_SIMD_VECTOR_EXT
#define TS_LANEWISE(F, T, L, NAME, OP) \
    TS_OP T F##_##NAME(T a, T b) { return a OP b; }
#else
#define TS_LANEWISE(F, T, L, NAME, OP) \
    TS_OP T F##_##NAME(T a, T b) { \
        T r; for (int k = 0; k < (L); k++) r.v[k] = a.v[k] OP b.v[k]; return r; }
#endif

/* load / store: from and to a numvec array's data at element i, unaligned-safe
 * (memcpy compiles to one vector move); splat: every lane x; lane: one lane. */
#if TUR_SIMD_VECTOR_EXT
#define TS_BASIC(F, T, E, L) \
    TS_OP T F##_load(const E *p) { T r; memcpy(&r, p, sizeof r); return r; } \
    TS_OP void F##_store(E *p, T a) { memcpy(p, &a, sizeof a); } \
    TS_OP T F##_splat(E x) { T r; for (int k = 0; k < (L); k++) r[k] = x; return r; } \
    TS_OP E F##_lane(T a, int k) { return a[k]; }
#else
#define TS_BASIC(F, T, E, L) \
    TS_OP T F##_load(const E *p) { T r; memcpy(r.v, p, sizeof r.v); return r; } \
    TS_OP void F##_store(E *p, T a) { memcpy(p, a.v, sizeof a.v); } \
    TS_OP T F##_splat(E x) { T r; for (int k = 0; k < (L); k++) r.v[k] = x; return r; } \
    TS_OP E F##_lane(T a, int k) { return a.v[k]; }
#endif

TS_BASIC(simd_f64x2, f64x2, double, 2)
TS_BASIC(simd_f64x4, f64x4, double, 4)
TS_BASIC(simd_f32x4, f32x4, float, 4)
TS_BASIC(simd_i32x4, i32x4, int32_t, 4)
TS_BASIC(simd_u8x16, u8x16, uint8_t, 16)
TS_BASIC(simd_u16x8, u16x8, uint16_t, 8)

TS_LANEWISE(simd_f64x2, f64x2, 2, add, +)
TS_LANEWISE(simd_f64x2, f64x2, 2, mul, *)
TS_LANEWISE(simd_f64x4, f64x4, 4, add, +)
TS_LANEWISE(simd_f64x4, f64x4, 4, sub, -)
TS_LANEWISE(simd_f64x4, f64x4, 4, mul, *)
TS_LANEWISE(simd_f64x4, f64x4, 4, div, /)
TS_LANEWISE(simd_f32x4, f32x4, 4, add, +)
TS_LANEWISE(simd_f32x4, f32x4, 4, sub, -)
TS_LANEWISE(simd_f32x4, f32x4, 4, mul, *)
TS_LANEWISE(simd_f32x4, f32x4, 4, div, /)
TS_LANEWISE(simd_i32x4, i32x4, 4, add, +)
TS_LANEWISE(simd_i32x4, i32x4, 4, sub, -)
TS_LANEWISE(simd_u16x8, u16x8, 8, add, +)
TS_LANEWISE(simd_u16x8, u16x8, 8, mul, *)

/* min / max: `a < b ? a : b` per lane, which never lets a NaN in (as N1's
 * lanes).  Spelled per lane on both lowerings: gcc's vector `?:` is C++-only. */
#if TUR_SIMD_VECTOR_EXT
#define TS_LANE(a, k) (a)[k]
#else
#define TS_LANE(a, k) (a).v[k]
#endif
#define TS_MINMAX(F, T, L) \
    TS_OP T F##_min(T a, T b) { \
        T r = a; \
        for (int k = 0; k < (L); k++) { \
            TS_LANE(r, k) = TS_LANE(b, k) < TS_LANE(a, k) ? TS_LANE(b, k) : TS_LANE(a, k); \
        } \
        return r; } \
    TS_OP T F##_max(T a, T b) { \
        T r = a; \
        for (int k = 0; k < (L); k++) { \
            TS_LANE(r, k) = TS_LANE(b, k) > TS_LANE(a, k) ? TS_LANE(b, k) : TS_LANE(a, k); \
        } \
        return r; }
TS_MINMAX(simd_f64x4, f64x4, 4)
TS_MINMAX(simd_f32x4, f32x4, 4)

/* shr: a logical right shift of every u16 lane by a constant count. */
#if TUR_SIMD_VECTOR_EXT
TS_OP u16x8 simd_u16x8_shr(u16x8 a, int n) { return a >> (uint16_t)n; }
#else
TS_OP u16x8 simd_u16x8_shr(u16x8 a, int n) {
    u16x8 r; for (int k = 0; k < 8; k++) r.v[k] = (uint16_t)(a.v[k] >> n); return r; }
#endif

/* Widening and narrowing: u8x16 <-> two u16x8 (lanes 0-7, 8-15).  S1 names
 * these as functions, never implicit.  The narrowing truncates each lane. */
#if TUR_SIMD_VECTOR_EXT
typedef uint8_t ts_u8x8 __attribute__((vector_size(8)));
TS_OP u16x8 simd_u8x16_widen_lo(u8x16 a) {
    ts_u8x8 h = __builtin_shufflevector(a, a, 0, 1, 2, 3, 4, 5, 6, 7);
    return __builtin_convertvector(h, u16x8); }
TS_OP u16x8 simd_u8x16_widen_hi(u8x16 a) {
    ts_u8x8 h = __builtin_shufflevector(a, a, 8, 9, 10, 11, 12, 13, 14, 15);
    return __builtin_convertvector(h, u16x8); }
TS_OP u8x16 simd_u16x8_narrow(u16x8 lo, u16x8 hi) {
    ts_u8x8 l = __builtin_convertvector(lo, ts_u8x8);
    ts_u8x8 h = __builtin_convertvector(hi, ts_u8x8);
    return __builtin_shufflevector(l, h, 0, 1, 2, 3, 4, 5, 6, 7,
                                   8, 9, 10, 11, 12, 13, 14, 15); }
#else
TS_OP u16x8 simd_u8x16_widen_lo(u8x16 a) {
    u16x8 r; for (int k = 0; k < 8; k++) r.v[k] = a.v[k]; return r; }
TS_OP u16x8 simd_u8x16_widen_hi(u8x16 a) {
    u16x8 r; for (int k = 0; k < 8; k++) r.v[k] = a.v[8 + k]; return r; }
TS_OP u8x16 simd_u16x8_narrow(u16x8 lo, u16x8 hi) {
    u8x16 r; for (int k = 0; k < 8; k++) { r.v[k] = (uint8_t)lo.v[k]; r.v[8 + k] = (uint8_t)hi.v[k]; }
    return r; }
#endif

/* hsum: the lanes added in one fixed order -- ((0 + 1) + (2 + 3)).  hsum2
 * takes two f64x4 accumulators holding lanes 0-3 and 4-7 of an eight-lane
 * sum and adds them in N1's tree, ((0+1)+(2+3)) + ((4+5)+(6+7)). */
TS_OP double simd_f64x4_hsum(f64x4 a) {
    return (TS_LANE(a, 0) + TS_LANE(a, 1)) + (TS_LANE(a, 2) + TS_LANE(a, 3)); }
TS_OP float simd_f32x4_hsum(f32x4 a) {
    return (TS_LANE(a, 0) + TS_LANE(a, 1)) + (TS_LANE(a, 2) + TS_LANE(a, 3)); }
TS_OP double simd_f64x4_hsum2(f64x4 lo, f64x4 hi) {
    return simd_f64x4_hsum(lo) + simd_f64x4_hsum(hi); }
TS_OP float simd_f32x4_hsum2(f32x4 lo, f32x4 hi) {
    return simd_f32x4_hsum(lo) + simd_f32x4_hsum(hi); }

#endif /* TUR_SIMD_H */
