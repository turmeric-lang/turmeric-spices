# tur-linalg

Dense float linear algebra for Turmeric: matrices, vectors, decompositions (Cholesky, LU, QR), and graphics helpers (vec2/vec3/vec4, mat4).

## Overview

`tur-linalg` is a Tier 1 spice (pure Turmeric + inline-C only) providing the
linear algebra primitives needed across the Turmeric spice ecosystem. It is
a self-contained spice with no external C dependencies.

Key features:
- Dynamic `:float` matrices and vectors (row-major storage)
- Matrix arithmetic: add, sub, scale, multiply, transpose
- Vector arithmetic: add, sub, scale, dot product, norms
- Decompositions: Cholesky (SPD systems), LU with partial pivoting, QR via Householder
- Linear system solvers: forward/back substitution, `mat-solve`, `mat-inv`
- Fixed-size graphics helpers: `vec2`, `vec3`, `vec4`, `mat4` with transforms
- Pretty-printing for matrices and vectors

## Install

```turmeric
:spices {
  "linalg" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "linalg-v0.1.0"
            :subdir "spices/linalg"}
}
```

Or use `tur add`:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref linalg-v0.1.0 --subdir spices/linalg --name linalg
```

## Quick start

```turmeric
(import linalg/mat :refer [mat-new mat-from-list mat-mul mat-print mat-free])
(import linalg/vec :refer [vec-from-list vec-dot vec-free])

;; Create matrices
(let [A (mat-from-list 2 2 (list 1.0 2.0 3.0 4.0))
      B (mat-from-list 2 2 (list 5.0 6.0 7.0 8.0))
      C (mat-mul A B)]
  (mat-print C)
  (mat-free A)
  (mat-free B)
  (mat-free C))
```

## Modules

| Module | Description |
|--------|-------------|
| `linalg/mat` | Dynamic float matrix type and arithmetic |
| `linalg/vec` | Dynamic float vector type and arithmetic |
| `linalg/decomp` | Cholesky, LU, QR decompositions |
| `linalg/solve` | Linear system solvers and matrix inverse |
| `linalg/small` | Fixed-size types: vec2/vec3/vec4, mat4 with transforms |
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
