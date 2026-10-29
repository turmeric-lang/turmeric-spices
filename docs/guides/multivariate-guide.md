# tur-stats-multivariate Guide

Multivariate statistics over [`tur-linalg`](linalg-guide.md) matrices:
covariance and correlation matrices, principal component analysis,
exploratory factor analysis, multivariate regression, Hotelling's T^2,
one-way MANOVA and linear discriminant analysis.

## Table of Contents

1. [Data layout and ownership](#1-data-layout-and-ownership)
2. [Covariance, correlation and Mahalanobis distance](#2-covariance-correlation-and-mahalanobis-distance)
3. [Principal component analysis](#3-principal-component-analysis)
4. [Factor analysis](#4-factor-analysis)
5. [Multivariate regression](#5-multivariate-regression)
6. [Hotelling's T^2 and MANOVA](#6-hotellings-t2-and-manova)
7. [Linear discriminant analysis](#7-linear-discriminant-analysis)
8. [Printing results](#8-printing-results)
9. [Limits in v0.1](#9-limits-in-v01)

---

## 1. Data layout and ownership

Every function takes a data matrix X as a linalg `mat` with **one
observation per row and one variable per column** (m x p). Group and class
labels are a `(Vec int)` with one entry per row, numbered `0 .. k-1`.

Results are structs. Those that own matrices -- `PcaResult`, `FaResult`,
`MvOlsResult`, `LdaResult` -- are move-only and have a matching `-free`;
`HotellingResult` and `ManovaResult` are plain values with nothing to free.
Functions that can be handed malformed input (`factor-analysis`,
`manova-1way`, `lda-fit`) return `(Result T cstr)` with a message in `Err`.

The numerical core is linalg's: PCA factors through `svd`, factor analysis,
MANOVA and LDA through `eigen-sym`, regression through `qr`. Build linalg with
its BLAS backend ([linalg guide, section 10](linalg-guide.md#10-the-blaslapack-backend))
and all of them speed up with no change here.

---

## 2. Covariance, correlation and Mahalanobis distance

```turmeric
(import linalg/mat :refer [mat-of mat-get mat-free])
(import linalg/vec :refer [la-vec-get la-vec-free])
(import multivariate/cov :refer [cov-mat cor-mat col-means mahalanobis-auto])

(let [x (mat-of 4 2 1.0 2.0 2.0 4.1 3.0 5.9 4.0 8.2)
      s (cov-mat x)
      r (cor-mat x)
      d (mahalanobis-auto x)]
  (println (mat-get s 0 1))       ; sample covariance of the two columns
  (println (mat-get r 0 1))       ; their correlation
  (println (la-vec-get d 0))      ; distance of row 0 from the centroid
  (la-vec-free d)
  (mat-free r)
  (mat-free s)
  (mat-free x))
```

- `cov-mat` divides by m - 1, `cov-mat-pop` by m; `cor-mat` has a unit
  diagonal. They agree with `stats/cov`'s pairwise `cov` / `cor`.
- `mahalanobis x mu s-inv` returns the **distance**
  `sqrt((x - mu)' S^-1 (x - mu))` per row, not its square (R's
  `mahalanobis` returns the square).
- `mahalanobis-auto` uses the column means and the SVD pseudo-inverse of the
  sample covariance (`mv-pinv`), so a constant or collinear column does not
  make it fail.

---

## 3. Principal component analysis

`pca` centres X (and optionally scales it to unit variance), takes its SVD,
and keeps `ncomp` components (`0` keeps all of them):

```turmeric
(import multivariate/pca :refer [pca pca-opts pca-free pca-project pca-reconstruct
                                 pca-var-explained])

(let [r (pca x (pca-opts true false 2))]   ; center, don't scale, keep 2
  (println (pca-var-explained r 1))        ; share of variance in PC1
  (println (la-vec-get (.sdev r) 0))       ; its standard deviation
  (pca-free r))
```

| Field | Shape | Meaning |
|---|---|---|
| `loadings` | n x k | principal directions, unit columns |
| `scores` | m x k | X's rows in component coordinates |
| `sdev` | k | standard deviation of each component |
| `var-prop` / `var-cum` | k | share of total variance, and its running sum |
| `center` / `scale` | n | what was subtracted / divided (zeros / ones when off) |

Each loading column is signed so its largest-magnitude entry is positive,
so results are reproducible. `pca-project r x-new` scores new rows the way
the fit was centred and scaled; `pca-reconstruct r kk` rebuilds X from its
first `kk` components (all of them recover X exactly).

Choose `scale = true` when the variables are on different scales: that is
PCA on the correlation matrix.

---

## 4. Factor analysis

`factor-analysis` fits `R ~ L L' + Psi` to a **correlation matrix** by
principal axis factoring: start from the squared multiple correlations,
then repeatedly put the current communalities on R's diagonal, take its k
leading eigenpairs as the loadings, and update the communalities, until
they settle.

```turmeric
(import multivariate/factor :refer [factor-analysis fa-opts fa-free fa-scores])

(let [r (cor-mat x)]
  (match (factor-analysis r (fa-opts 2))
    (Ok f)  (do (println (la-vec-get (.communality f) 0))   ; h2 of variable 1
                (println (.converged f))
                (fa-free f))
    (Err e) (println e))
  (mat-free r))
```

`(fa-opts k)` is principal axis factoring, unrotated, at most 500 iterations
to a tolerance of 1e-9; build an `FAOpts` directly for other limits.
`communality + uniqueness = 1` for every variable. A run that hits the
iteration limit is still `Ok`, with `converged` false. `fa-scores f x`
gives regression (Thomson) factor scores for the data R came from.

---

## 5. Multivariate regression

`mv-ols` fits q responses at once, `Y = X B + E`, with one QR of X. Put a
column of ones in X for an intercept.

```turmeric
(import multivariate/regress :refer [mv-ols mv-ols-free mv-predict mv-r-squared])

(let [x  (mat-of 4 2 1.0 0.0 1.0 1.0 1.0 2.0 1.0 3.0)   ; intercept, t
      y  (mat-of 4 2 1.0 9.8 3.1 8.1 4.9 6.0 7.0 4.1)   ; two responses
      r  (mv-ols x y)
      r2 (mv-r-squared r)]
  (println (mat-get (.coef r) 1 0))   ; slope of response 0
  (println (la-vec-get r2 1))         ; R^2 of response 1
  (la-vec-free r2)
  (mv-ols-free r)
  (mat-free y)
  (mat-free x))
```

The result holds `coef` (p x q), `fitted`, `residuals`, `rss-mat` (the
residual SSCP matrix E'E, q x q) and `df-resid` (m - p). Each response's
coefficients equal what `stats/regress`'s single-response `ols` gives.

---

## 6. Hotelling's T^2 and MANOVA

```turmeric
(import multivariate/test :refer [hotelling-1samp hotelling-2samp manova-1way])

;; one sample: does the mean of x equal mu0?
(let [mu0 (la-vec-of 5.5 3.0)
      t   (hotelling-1samp x mu0)]
  (println (.p-value t))
  (la-vec-free mu0))

;; two samples: are the means of a and b equal?
(println (.p-value (hotelling-2samp a b)))

;; k groups, one label per row of x
(match (manova-1way x groups)
  (Ok m)  (do (println (.wilks m))
              (println (.p-value m)))
  (Err e) (println e))
```

- The T^2 tests convert to an exact F: `(m - p) / (p (m - 1)) T^2` on
  (p, m - p) df for one sample, `(n1 + n2 - p - 1) / (p (n1 + n2 - 2)) T^2`
  on (p, n1 + n2 - p - 1) df for two (pooled covariance).
- `manova-1way` reports Wilks' lambda with Rao's F approximation (its `df2`
  need not be an integer) and Pillai's trace. It returns `Err` for malformed
  labels, too few observations, or a singular within-groups SSCP matrix.
- p-values come from `stats/dist`'s `pf-dist`; the tests check it against
  tabulated F critical values and the statistics against scipy.

---

## 7. Linear discriminant analysis

```turmeric
(import multivariate/lda :refer [lda-fit lda-free lda-opts-default lda-opts-free
                                 lda-predict lda-posterior lda-transform])

(let [o (lda-opts-default)]
  (match (lda-fit x y o)
    (Ok r)  (let [p (lda-predict r x-new)]   ; (Vec int) of classes
              (println (vec-get p 0))
              (vec-free p)
              (lda-free r))
    (Err e) (println e))
  (lda-opts-free o))
```

LDA pools the within-class covariance S_w and finds the axes that best
separate the class means relative to it. It solves `S_w^-1 S_b a = lambda a`
by whitening -- `W = S_w^-1/2`, then `eigen-sym` of `W S_b W` -- so the
d = min(k - 1, p) axes in `scalings` come out S_w-orthonormal, the scaling R's
`MASS::lda` uses. Prediction is the shared-covariance Gaussian classifier;
`lda-posterior` gives each row's class probabilities and `lda-transform` the
coordinates on the discriminant axes.

Priors default to the class frequencies; pass your own with
`(lda-opts (la-vec-of 0.2 0.3 0.5) 1e-08)`. The second argument is the
near-singularity threshold: when S_w's smallest eigenvalue falls below it
times the largest, a ridge of that size times `trace(S_w) / p` is added to
the diagonal and `regularized` reads true.

---

## 8. Printing results

`multivariate/fmt` has a `*->str` (caller frees with `mv-str-free`) and a
`*-print` for each result: `pca`, `fa`, `mv-regress`, `hotelling`, `manova`
and `lda`, plus `pca-print-loadings r k`.

```text
Importance of components:
               sdev       prop cumulative
PC1          2.0195     0.9987     0.9987
PC2          0.0715     0.0013     1.0000
Loadings:
                PC1        PC2
V1           0.4942     0.8693
V2           0.8693    -0.4942
```

---

## 9. Limits in v0.1

| Not yet | Planned |
|---|---|
| Varimax / oblimin rotation, maximum-likelihood factor analysis | v0.2 |
| Canonical correlation analysis, quadratic discriminant analysis | v0.2 |
| k-means, hierarchical clustering, multidimensional scaling | v0.3 |

---

## See also

- [tur-linalg guide](linalg-guide.md) -- the matrix types and decompositions underneath
- [tur-stats](https://spices.turmeric-lang.com/stats/) -- univariate statistics and distributions
