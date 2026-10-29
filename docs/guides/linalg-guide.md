# tur-linalg Guide

Dense float linear algebra for Turmeric. This guide walks through the key
features of the `tur-linalg` spice: creating matrices and vectors, solving
linear systems, computing decompositions, and using the graphics helpers.

## Table of Contents

1. [Creating and manipulating matrices](#1-creating-and-manipulating-matrices)
2. [Solving linear systems](#2-solving-linear-systems)
3. [Least-squares fitting with QR](#3-least-squares-fitting-with-qr)
4. [Graphics transforms](#4-graphics-transforms)
5. [Memory model](#5-memory-model)
6. [Numerical accuracy and limitations](#6-numerical-accuracy-and-limitations)
7. [SVD and eigendecomposition](#7-svd-and-eigendecomposition)
8. [Iterative solvers](#8-iterative-solvers)
9. [Sparse matrices](#9-sparse-matrices)
10. [The BLAS/LAPACK backend](#10-the-blaslapack-backend)

---

## 1. Creating and manipulating matrices

### Basic matrix construction

```turmeric
(import linalg/mat :refer [mat-new mat-new-zeroed mat-identity mat-from-list
                            mat-free mat-print])

;; Create a 3x4 uninitialized matrix
(let [A (mat-new 3 4)]
  (mat-free A))

;; Create a 2x2 zero matrix
(let [B (mat-new-zeroed 2 2)]
  (mat-print B)  ;; prints: 0.0000 0.0000
              ;;         0.0000 0.0000
  (mat-free B))

;; Create a 3x3 identity matrix
(let [I (mat-identity 3)]
  (mat-print I)  ;; prints: 1.0000 0.0000 0.0000
              ;;         0.0000 1.0000 0.0000
              ;;         0.0000 0.0000 1.0000
  (mat-free I))

;; Create a matrix from a list (row-major)
(let [C (mat-from-list 2 3 (cons 1.0 (cons 2.0 (cons 3.0
                                    (cons 4.0 (cons 5.0 (cons 6.0 0)))))))]
  (mat-print C)  ;; prints: 1.0000 2.0000 3.0000
              ;;         4.0000 5.0000 6.0000
  (mat-free C))
```

### Matrix arithmetic

```turmeric
(import linalg/mat :refer [mat-add mat-sub mat-scale mat-mul mat-transpose
                            mat-trace mat-norm-fro mat-copy])

(let [A (mat-from-list 2 2 (cons 1.0 (cons 2.0 (cons 3.0 (cons 4.0 0)))))
      B (mat-from-list 2 2 (cons 5.0 (cons 6.0 (cons 7.0 (cons 8.0 0)))))]
  
  ;; Addition
  (let [C (mat-add A B)]
    (mat-print C)  ;; prints: 6.0000 8.0000
                ;;         10.0000 12.0000
    (mat-free C))
  
  ;; Scalar multiplication
  (let [C (mat-scale A 2.0)]
    (mat-print C)  ;; prints: 2.0000 4.0000
                ;;         6.0000 8.0000
    (mat-free C))
  
  ;; Matrix multiplication
  (let [D (mat-from-list 2 3 (cons 1.0 (cons 2.0 (cons 3.0
                                    (cons 4.0 (cons 5.0 (cons 6.0 0)))))))
        E (mat-from-list 3 2 (cons 7.0 (cons 8.0 (cons 9.0
                                    (cons 10.0 (cons 11.0 (cons 12.0 0)))))))
        F (mat-mul D E)]
    (mat-print F)  ;; prints: 58.0000 64.0000
                ;;         139.0000 154.0000
    (mat-free D)
    (mat-free E)
    (mat-free F))
  
  ;; Transpose
  (let [C (mat-transpose A)]
    (mat-print C)  ;; prints: 1.0000 3.0000
                ;;         2.0000 4.0000
    (mat-free C))
  
  ;; Trace (sum of diagonal)
  (println (mat-trace A))  ;; prints: 5.0
  
  ;; Frobenius norm
  (println (mat-norm-fro A))  ;; prints: 5.4772 (sqrt(1+4+9+16))
  
  (mat-free A)
  (mat-free B))
```

### Vector operations

```turmeric
(import linalg/vec :refer [vec-new vec-from-list vec-free vec-add vec-sub
                            vec-scale vec-dot vec-norm vec-normalize])

(let [a (vec-from-list (cons 1.0 (cons 2.0 (cons 3.0 0))))
      b (vec-from-list (cons 4.0 (cons 5.0 (cons 6.0 0)))]
  
  ;; Addition
  (let [c (vec-add a b)]
    (vec-print c)  ;; prints: (5.0000 7.0000 9.0000)
    (vec-free c))
  
  ;; Dot product
  (println (vec-dot a b))  ;; prints: 32.0 (1*4 + 2*5 + 3*6)
  
  ;; Norm
  (println (vec-norm a))  ;; prints: 3.7417 (sqrt(1+4+9))
  
  ;; Normalize
  (let [u (vec-normalize a)]
    (vec-print u)  ;; prints: (0.2673 0.5345 0.8018)
    (vec-free u))
  
  (vec-free a)
  (vec-free b))
```

---

## 2. Solving linear systems

### Cholesky decomposition (for SPD matrices)

```turmeric
(import linalg/decomp :refer [chol chol-free])
(import linalg/solve :refer [chol-solve])

;; Solve Ax = b where A is symmetric positive definite
(let [A (mat-from-list 3 3 (cons 4.0 (cons -1.0 (cons 2.0
                                    (cons -1.0 (cons 5.0 (cons 0.0
                                    (cons 2.0 (cons 0.0 (cons 3.0 0)))))))))]
      b (vec-from-list (cons 1.0 (cons 2.0 (cons 3.0 0))))
      
      ;; Factorize A = L L'
      f (chol A)
      
      ;; Solve using the factor
      x (chol-solve f b)]
  
  (vec-print x)  ;; prints solution
  
  (chol-free f)
  (mat-free A)
  (vec-free b)
  (vec-free x))
```

### LU decomposition (for general square matrices)

```turmeric
(import linalg/decomp :refer [lu lu-free])
(import linalg/solve :refer [lu-solve mat-solve])

;; Solve Ax = b where A is any square non-singular matrix
(let [A (mat-from-list 3 3 (cons 1.0 (cons 2.0 (cons 3.0
                                    (cons 4.0 (cons 5.0 (cons 6.0
                                    (cons 7.0 (cons 8.0 (cons 9.0 0)))))))))]
      b (vec-from-list (cons 1.0 (cons 0.0 (cons 0.0 0))))
      
      ;; Option 1: Factorize and solve separately
      f (lu A)
      x (lu-solve f b)
      
      ;; Option 2: Factorize and solve in one call (automatically chooses LU)
      x2 (mat-solve A b 0)]
  
  (vec-print x)
  (vec-print x2)
  
  (lu-free f)
  (mat-free A)
  (vec-free b)
  (vec-free x)
  (vec-free x2))
```

### Matrix inverse and determinant

```turmeric
(import linalg/solve :refer [mat-inv mat-det])

(let [A (mat-from-list 2 2 (cons 1.0 (cons 2.0 (cons 3.0 (cons 4.0 0)))))
      
      ;; Compute inverse
      Ainv (mat-inv A)
      
      ;; Compute determinant
      det (mat-det A)]
  
  (println "Inverse:")
  (mat-print Ainv)
  
  (println "Determinant:")
  (println det)  ;; prints: -2.0
  
  (mat-free A)
  (mat-free Ainv))
```

---

## 3. Least-squares fitting with QR

```turmeric
(import linalg/decomp :refer [qr qr-free])
(import linalg/solve :refer [qr-solve])

;; Solve min ||Ax - b|| for overdetermined system (m > n)
;; This is the least-squares solution
(let [A (mat-from-list 6 3 (cons 1.0 (cons 1.0
                                    (cons 1.0 (cons 1.0
                                    (cons 1.0 (cons 1.0
                                    (cons 2.0 (cons 2.0
                                    (cons 2.0 (cons 3.0
                                    (cons 3.0 (cons 3.0 0))))))))))))]
      b (vec-from-list (cons 1.0 (cons 2.0 (cons 3.0
                                   (cons 4.0 (cons 5.0 (cons 6.0 0))))))
      
      ;; Factorize A = QR
      f (qr A)
      
      ;; Solve for least-squares solution
      x (qr-solve f b)]
  
  (vec-print x)
  
  (qr-free f)
  (mat-free A)
  (vec-free b)
  (vec-free x))
```

---

## 4. Graphics transforms

### Basic transforms

```turmeric
(import linalg/small :refer [vec3 mat4-identity mat4-translate mat4-scale
                               mat4-rotate-x mat4-rotate-y mat4-rotate-z
                               mat4-mul mat4-print mat4-ptr])

(let [eye (vec3 0.0 0.0 5.0)
      center (vec3 0.0 0.0 0.0)
      up (vec3 0.0 1.0 0.0)
      
      ;; Create transform matrices
      T (mat4-translate eye)
      Rx (mat4-rotate-x 0.5)
      Ry (mat4-rotate-y 1.0)
      
      ;; Combine: model = Ry * Rx * T
      model (mat4-mul (mat4-mul Ry Rx) T)]
  
  (mat4-print model)
  
  (free eye)
  (free center)
  (free up)
  (free T)
  (free Rx)
  (free Ry)
  (free model))
```

### Projection matrices

```turmeric
(import linalg/small :refer [mat4-perspective mat4-ortho mat4-look-at])

(let [;; Perspective projection
      proj (mat4-perspective 1.0  ; fovy (radians)
                            16.0/9.0  ; aspect
                            0.1       ; near
                            100.0)    ; far
      
      ;; View matrix
      eye (vec3 0.0 0.0 5.0)
      center (vec3 0.0 0.0 0.0)
      up (vec3 0.0 1.0 0.0)
      view (mat4-look-at eye center up)
      
      ;; Orthographic projection
      ortho (mat4-ortho -10.0 10.0  ; left, right
                         -10.0 10.0  ; bottom, top
                         0.1 100.0)] ; near, far
  
  (mat4-print proj)
  (mat4-print view)
  (mat4-print ortho)
  
  (free eye)
  (free center)
  (free up)
  (free proj)
  (free view)
  (free ortho))
```

### Passing to OpenGL

```turmeric
(import linalg/small :refer [mat4-ptr vec3-ptr])

;; mat4-ptr returns a pointer suitable for glUniformMatrix4fv
;; The pointer is valid only for the duration of the expression
(let [m (mat4-identity)
      ptr (mat4-ptr m)]
  ;; glUniformMatrix4fv(location, 1, GL_FALSE, ptr)
  (free m))
```

---

## 5. Memory model

### Ownership of matrices and vectors

All `mat` and `vec` types are heap-allocated. You must call `mat-free` or
`vec-free` to release memory when you're done with them:

```turmeric
(let [m (mat-new 10 10)]
  ;; ... use m ...
  (mat-free m))  ; Must free!
```

### Ownership of factor structs

Decomposition factor structs (`chol-factor`, `lu-factor`, `qr-factor`) own
their component matrices. You must free the factor to release all memory:

```turmeric
(let [A (mat-from-list 3 3 ...)
      f (chol A)]
  ;; ... use f ...
  (chol-free f)  ; Frees both the factor and the internal L matrix
  (mat-free A))
```

**Important:** Do not free the component matrices directly. Always free
through the factor's free function.

### Stack lifetime of small types

The `vec2`, `vec3`, `vec4`, `mat4` types from `linalg/small` are heap-allocated
in Turmeric (via `malloc`), so they follow the same ownership rules as `mat`
and `vec`. However, their pointers (`mat4-ptr`, `vec3-ptr`) are only valid for
the duration of the calling expression:

```turmeric
(let [m (mat4-identity)]
  (let [ptr (mat4-ptr m)]
    ;; Use ptr immediately
    )
  ;; ptr is no longer valid here!
  (free m))
```

---

## 6. Numerical accuracy and limitations

### Precision

- `linalg/mat` and `linalg/vec`: 64-bit floats (`double`)
- `linalg/small`: 32-bit floats (`float`)

### Algorithm limitations

- **Cholesky**: Requires symmetric positive-definite matrices. The algorithm
  detects non-SPD matrices by checking for non-positive pivots.
- **LU**: Uses partial pivoting. Detects singular matrices by checking for
  zero pivots within tolerance (1e-12).
- **QR**: Uses Householder reflections. Handles rank-deficient matrices by
  skipping zero columns.

### Matrix sizes

The numeric kernels run in C over the matrix buffers, and since v0.2
`mat-mul` is cache-blocked (a 512x512 product takes on the order of 100 ms).
For larger work:

1. Build with the optional BLAS/LAPACK backend ([section 10](#10-the-blaslapack-backend))
2. Use the iterative solvers for large SPD or diagonally dominant systems
   ([section 8](#8-iterative-solvers))
3. Store mostly-zero matrices in `tur-linalg-sparse` ([section 9](#9-sparse-matrices))

### Tolerances

- Cholesky and LU check for zero pivots with tolerance 1e-12
- `vec-normalize` rejects zero vectors with norm < 1e-6
- `mat-approx-eq?` and `vec-approx-eq?` accept a user-specified tolerance

---

## 7. SVD and eigendecomposition

Added in v0.2. Both live in `linalg/decomp`; the SVD helpers live in
`linalg/solve`.

### Singular value decomposition

`svd` factors any m x n matrix, tall or wide, as `A = U diag(s) Vt`. `U` is
m x m and `Vt` is n x n, both orthogonal; `s` holds the min(m, n) singular
values in descending order. It never fails. The pure path uses one-sided
(Hestenes) Jacobi rotations, which is accurate to high relative precision
but costs O(m n^2) per sweep, so it suits matrices up to a few hundred on a
side. The BLAS backend routes it to LAPACK's `dgesdd`.

```turmeric
(import linalg/mat :refer [mat-of mat-free])
(import linalg/vec :refer [la-vec-of la-vec-get la-vec-free])
(import linalg/decomp :refer [svd svd-free])
(import linalg/solve :refer [svd-solve svd-rank svd-condition svd-project])

(let [a (mat-of 3 2 1.0 2.0 2.0 4.0 3.0 6.0)   ; rank 1
      b (la-vec-of 1.0 2.0 3.0)
      f (svd a)
      x (svd-solve f b)]                        ; minimum-norm least squares
  (println (svd-rank f 0.0))                    ; 1
  (println (la-vec-get x 0))                    ; 0.2
  (la-vec-free x)
  (svd-free f)
  (la-vec-free b)
  (mat-free a))
```

- `svd-rank f tol` counts the singular values above `tol`; a `tol <= 0`
  uses the LAPACK/numpy cutoff `max(m, n) * eps * s[0]`.
- `svd-solve f b` minimizes `||A x - b||` and, among the minimizers, `||x||`,
  keeping only the singular values above that cutoff. It handles
  over-determined, under-determined and rank-deficient systems.
- `svd-condition f` is `s[0] / s[k-1]` (infinite for an exactly singular A).
- `svd-project f b` is the orthogonal projection of `b` onto A's column space.

### Symmetric eigendecomposition

`eigen-sym` returns `(Result eigenfac cstr)`: `Err` when the matrix is not
square or not symmetric, otherwise the eigenvalues in ascending order
(`vals`) and the matching unit eigenvectors as the columns of `vecs`, each
signed so its largest-magnitude component is positive. The pure path uses
cyclic Jacobi rotations; the BLAS backend uses `dsyev`.

```turmeric
(import linalg/decomp :refer [eigen-sym eigen-free])

(match (eigen-sym (mat-of 2 2 2.0 1.0 1.0 2.0))
  (Ok f)  (do (println (la-vec-get (.vals f) 0))   ; 1
              (println (la-vec-get (.vals f) 1))   ; 3
              (eigen-free f))
  (Err e) (println e))
```

### Small matrices

`linalg/small` gained `mat2-mul`, `mat2-inv`, `mat3-mul`, `mat3-inv` and
`mat3-normal-mat`, the transpose of the inverse of a `mat4`'s upper-left 3x3
block -- the matrix that carries surface normals under a model transform.

---

## 8. Iterative solvers

`linalg/iter` (v0.2) solves A x = b without factoring A: conjugate gradient
for symmetric positive-definite A, BiCGSTAB for any non-singular A. Both
start from x = 0 and stop when the residual `||b - A x||` drops below the
tolerance or the iteration limit is reached. They return an `IterResult`
that owns the solution:

| Field | Meaning |
|---|---|
| `x` | the solution, or the last iterate if it did not converge |
| `iters` | iterations used |
| `residual` | `||b - A x||` of the returned `x` |
| `converged` | `true` when `residual < tol` |

```turmeric
(import linalg/iter :refer [iter-opts iter-opts-default cg cg-free bicgstab bicgstab-free
                            jacobi-precond])

(let [r (cg a b (iter-opts 500 1e-10 false))]   ; max-iters, tol, verbose
  (when (not (.converged r))
    (println "cg did not converge"))
  (println (.iters r))
  (cg-free r))
```

`(iter-opts-default)` is 1000 iterations at tolerance 1e-10; set `verbose` to
print each iteration's residual to stderr. `jacobi-precond` returns the
reciprocals of A's diagonal, the vector a diagonal preconditioner applies.

---

## 9. Sparse matrices

`tur-linalg-sparse` is a separate spice for matrices that are mostly zeros.
It reuses linalg's `lavec` for vectors and `linalg/iter`'s `IterOpts` /
`IterResult` for its solvers.

Assemble a matrix as a coordinate list, then compress it. Entries may be
pushed in any order; a repeated (row, col) is summed:

```turmeric
(import linalg-sparse/csr :refer [coo-new coo-push! coo-free csr-from-coo csr-free
                                  csr-get csr-spmv])

(let [c (coo-new 3 3)]
  (coo-push! c 0 0 2.0)
  (coo-push! c 0 1 -1.0)
  (coo-push! c 1 1 1.5)
  (coo-push! c 1 1 0.5)                 ; summed: (1, 1) = 2.0
  (let [m (csr-from-coo c)]
    (println (csr-get m 1 1))           ; 2
    (println (csr-get m 2 0))           ; 0 -- not stored
    (csr-free m)
    (coo-free c)))
```

| Module | What it has |
|---|---|
| `linalg-sparse/csr` | `coomat` builder, `csrmat`, `csr-get`, `csr-spmv` (A v) |
| `linalg-sparse/csc` | `cscmat`, `csc-get`, `csc-spmv` (A' v), `csc-from-csr`, `csr-from-csc`, `csc-transpose` |
| `linalg-sparse/ops` | `csr-scale`, `csr-add` (union of patterns), `csr-transpose`, dense conversion both ways |
| `linalg-sparse/solve` | `ilu0`, `ilu-apply`, `sparse-cg`, `sparse-bicgstab` |
| `linalg-sparse/fmt` | `csr->str`, `csr-print`, `csc->str` -- `(row, col) value` lines |

Transposition is cheap: the CSR buffers of A are exactly the CSC buffers of
A', so `csr-transpose` and `csc-transpose` copy rather than re-sort.
Changing the storage of the *same* matrix (`csc-from-csr`) is a counting
sort, O(nnz + rows + cols).

`sparse-cg` and `sparse-bicgstab` take the same options as the dense
solvers but multiply through the CSR buffers, so one iteration costs
O(nnz) and A is never densified. Free their result with
`iter-result-free`:

```turmeric
(import linalg/iter :refer [iter-opts iter-result-free])
(import linalg-sparse/solve :refer [sparse-cg])

(let [r (sparse-cg a b (iter-opts 200 1e-10 false))]
  (println (.iters r))       ; a 100x100 1D Laplacian takes about 100
  (iter-result-free r))
```

`ilu0` is the incomplete LU factorization with zero fill-in: L (unit lower)
and U keep A's sparsity pattern exactly, and `ilu-apply f r` solves
`L U z = r`. It returns `(Result ilufac cstr)`, with `Err` for a non-square
matrix or a missing or zero diagonal pivot. On a matrix with no fill to
drop -- a tridiagonal one, say -- it is the exact LU.

---

## 10. The BLAS/LAPACK backend

`tur-linalg` can route its dense hot paths to a system BLAS/LAPACK. The
public API does not change and nothing else is affected: the backend is
compiled in only when the consuming project asks for it, and a build without
it references no BLAS symbol at all.

| Turmeric function | Backend routine |
|---|---|
| `mat-mul` | `cblas_dgemm` |
| `mat-mul-vec` | `cblas_dgemv` |
| `chol` | `dpotrf` |
| `lu` | `dgetrf` |
| `qr` | `dgeqrf` + `dorgqr` |
| `svd` | `dgesdd` |
| `eigen-sym` | `dsyev` |

To turn it on, define `TUR_LINALG_BLAS` and link a BLAS/LAPACK from your
project's `build.tur`:

```turmeric
;; Linux, with libopenblas-dev (or any library providing cblas_* and LAPACK)
:build-opts #map{:c-flags   ["-DTUR_LINALG_BLAS=1"]
                 :link-libs ["openblas"]}

;; macOS -- the system Accelerate framework
:build-opts #map{:c-flags    ["-DTUR_LINALG_BLAS=1"]
                 :link-flags ["-framework Accelerate"]}
```

The backend calls LAPACK's Fortran entry points (`dpotrf_` and so on) and
declares their prototypes itself, so it needs no `cblas.h` or `lapacke.h`
on any include path; OpenBLAS, MKL's compatibility layer, reference
BLAS/LAPACK and Accelerate all provide those symbols. It assumes the
default 32-bit LAPACK integer (an ILP64 build of OpenBLAS will not work).

`(linalg-blas?)` (in `linalg/mat`) reports whether the backend is active.
Setting the environment variable `TUR_LINALG_NO_BLAS` to a non-empty value
switches it off at run time without a rebuild -- handy for comparing the two
paths. The results agree to about 12 significant figures, with two
differences to expect:

- SVD singular vectors may differ in sign (any sign is a valid SVD);
  singular values agree.
- Pivoting, Householder sign and eigenvector sign conventions are the same
  as the pure kernels, so `lu`, `qr` and `eigen-sym` factors match entry by
  entry.

The cache-blocked pure `mat-mul` uses 64 x 64 tiles. Override that at build
time with `:c-flags ["-DTUR_LINALG_BLOCK=32"]`.

The spice's `fixtures/run.sh` builds the linalg test suite against the
backend, checks each routed operation against the pure kernels, and times
the 512x512 multiply both ways.

---

## See also

- [API Reference](https://spices.turmeric-lang.com/linalg/api/) - Detailed function documentation
- [tur-math](https://spices.turmeric-lang.com/math/) - Alternative vector/math library
- [tur-stats](https://spices.turmeric-lang.com/stats/) - Statistics library that uses tur-linalg
