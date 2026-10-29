# tur-linalg

Dense float linear algebra for Turmeric: matrices, vectors, decompositions (Cholesky, LU, QR, SVD, symmetric eigen), iterative solvers, an optional BLAS/LAPACK backend, and graphics helpers (vec2/vec3/vec4, mat2/mat3/mat4).

## Overview

`tur-linalg` is a Tier 1 spice (pure Turmeric + inline-C only) providing the
linear algebra primitives needed across the Turmeric spice ecosystem. It is
a self-contained spice with no external C dependencies.

Key features:
- Dynamic `:float` matrices and vectors (row-major storage)
- Matrix arithmetic: add, sub, scale, cache-blocked multiply, transpose
- Vector arithmetic: add, sub, scale, dot product, norms
- Decompositions: Cholesky (SPD systems), LU with partial pivoting, QR via
  Householder, SVD (one-sided Jacobi), symmetric eigendecomposition (Jacobi)
- Linear system solvers: forward/back substitution, `mat-solve`, `mat-inv`,
  least squares through QR or the rank-revealing SVD (`svd-solve`)
- Iterative solvers (`linalg/iter`): conjugate gradient and BiCGSTAB
- Optional BLAS/LAPACK backend: build with `-DTUR_LINALG_BLAS` and link
  OpenBLAS or Accelerate to route `mat-mul`, `chol`, `lu`, `qr`, `svd` and
  `eigen-sym` to `dgemm` / LAPACK -- same API
- Fixed-size graphics helpers: `vec2`, `vec3`, `vec4`, `mat2`, `mat3`, `mat4`
  with transforms, inverses and the normal matrix
- Pretty-printing for matrices and vectors

For sparse matrices see [`tur-linalg-sparse`](../linalg-sparse/).

## Install

```turmeric
:spices {
  "linalg" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "linalg-v0.2.0"
            :subdir "spices/linalg"}
}
```

Or use `tur add`:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref linalg-v0.2.0 --subdir spices/linalg --name linalg
```

## Quick start

```turmeric
(import linalg/mat :refer [mat-of mat-mul mat-free])
(import linalg/fmt :refer [mat-print])

(defn main [] : int
  (let [a (mat-of 2 2 1.0 2.0 3.0 4.0)
        b (mat-of 2 2 5.0 6.0 7.0 8.0)
        c (mat-mul a b)]
    (mat-print c)
    (mat-free a)
    (mat-free b)
    (mat-free c)
    0))
```

## Modules

| Module | Description |
|--------|-------------|
| `linalg/mat` | Dynamic float matrix type and arithmetic |
| `linalg/vec` | Dynamic float vector type and arithmetic |
| `linalg/decomp` | Cholesky, LU, QR, SVD and symmetric eigen decompositions |
| `linalg/solve` | Linear system solvers, matrix inverse, SVD least squares / rank / condition |
| `linalg/iter` | Iterative solvers: conjugate gradient, BiCGSTAB, Jacobi preconditioner |
| `linalg/small` | Fixed-size types: vec2/vec3/vec4, mat2/mat3/mat4 with transforms |
| `linalg/sized` | Length- and shape-indexed vectors and matrices (dimension errors at compile time) |
| `linalg/fmt` | Pretty-printing and string conversion |

## Numerical notes

All `linalg/mat` and `linalg/vec` values use 64-bit floats (`double`).
`linalg/small` types use 32-bit floats for GPU compatibility.

Matrix storage is row-major for `mat`. `mat4` in `linalg/small` uses
column-major storage to match OpenGL conventions.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/linalg-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/linalg>
