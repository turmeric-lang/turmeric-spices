# Linear Algebra Walkthrough

This notebook demonstrates the `tur-linalg` spice: matrix creation,
arithmetic, solving linear systems, and LU decomposition.

---

## Creating matrices and vectors

`mat-of` and `la-vec-of` are macros that build matrices and vectors from
row-major float literals.

```turmeric
(import linalg/mat :refer [mat-of mat-transpose mat-mul])
(import linalg/vec :refer [la-vec-of])
(import linalg/fmt :refer [mat-print vec-print linalg-str-free])

(def A (mat-of 2 2  1.0 2.0  3.0 4.0))
(def b (la-vec-of 5.0 6.0))

(mat-print A)
(vec-print b)
```

---

## Matrix arithmetic

```turmeric
(import linalg/mat :refer [mat-of mat-mul mat-transpose mat-scale])

(def A (mat-of 2 2  1.0 2.0  3.0 4.0))
(def B (mat-of 2 2  5.0 6.0  7.0 8.0))

;; Matrix product
(mat-print (mat-mul A B))

;; Transpose
(mat-print (mat-transpose A))

;; Scalar multiply
(mat-print (mat-scale A 2.0))
```

---

## Solving a linear system

`mat-solve` takes a matrix, a right-hand-side vector, and a flag
(`0` = general, `1` = symmetric positive definite). It returns the
solution vector.

```turmeric
(import linalg/mat  :refer [mat-of])
(import linalg/vec  :refer [la-vec-of])
(import linalg/solve :refer [mat-solve])

;; Solve A x = b for the 2x2 system:
;;   1x + 2y = 5
;;   3x + 4y = 6
(def A (mat-of 2 2  1.0 2.0  3.0 4.0))
(def b (la-vec-of 5.0 6.0))

(vec-print (mat-solve A b 0))
;; => -4.0000  4.5000
```

---

## LU decomposition

`lu` factors a matrix into lower and upper triangular parts. `lu-solve`
uses the factorization to solve a system, which is useful when you need
to solve multiple systems with the same matrix.

```turmeric
(import linalg/mat   :refer [mat-of])
(import linalg/vec   :refer [la-vec-of])
(import linalg/decomp :refer [lu lu-free])
(import linalg/solve  :refer [lu-solve])

(def A (mat-of 3 3  2.0 1.0 0.0  0.0 3.0 1.0  1.0 0.0 2.0))
(def b1 (la-vec-of 1.0 2.0 3.0))
(def b2 (la-vec-of 4.0 5.0 6.0))

(def fac (lu A))

;; Solve A x1 = b1
(vec-print (lu-solve fac b1))

;; Solve A x2 = b2 (reuses the factorization)
(vec-print (lu-solve fac b2))

(lu-free fac)
```

---

## Least-squares fit

For an overdetermined system, use QR to find the least-squares solution.
Here we fit a line y = a + b*x to four data points.

```turmeric
(import linalg/mat  :refer [mat-of mat-transpose mat-mul])
(import linalg/vec  :refer [la-vec-of])
(import linalg/solve :refer [qr-solve])
(import linalg/decomp :refer [qr qr-free])

;; Data: (1, 2.1), (2, 3.9), (3, 6.2), (4, 8.1)
;; Design matrix: [1 x; 1 x; 1 x; 1 x]
(def X (mat-of 4 2  1.0 1.0  1.0 2.0  1.0 3.0  1.0 4.0))
(def y (la-vec-of 2.1 3.9 6.2 8.1))

;; Least-squares: solve X^T X beta = X^T y
(def Xt (mat-transpose X))
(def XtX (mat-mul Xt X))
(def Xty (mat-mul-vec Xt y))

(vec-print (mat-solve XtX Xty 1))
;; => intercept ~ 0.00, slope ~ 2.03
```
