# tur-stats-multivariate

Multivariate statistics for Turmeric over [`tur-linalg`](../linalg/) matrices:
covariance and correlation matrices, Mahalanobis distance, principal
component analysis, exploratory factor analysis, multivariate OLS,
Hotelling's T^2, one-way MANOVA and linear discriminant analysis.

## Overview

Data come in as a linalg `mat` with one observation per row. The heavy
linear algebra is linalg's: PCA factors through `svd`, factor analysis, MANOVA
and LDA through `eigen-sym`, regression through `qr`. F p-values come from
[`tur-stats`](../stats/)' `stats/dist` (`pf-dist`).

Key features:
- `cov-mat`, `cov-mat-pop`, `cor-mat`, `mahalanobis`, `mahalanobis-auto`
- `pca` with centring / scaling / truncation, `pca-project`, `pca-reconstruct`
- `factor-analysis` by principal axis factoring, `fa-scores`
- `mv-ols` for q responses at once, `mv-predict`, `mv-r-squared`
- `hotelling-1samp`, `hotelling-2samp`, `manova-1way` (Wilks, Pillai, Rao F)
- `lda-fit` with a ridge for near-singular covariance, `lda-predict`,
  `lda-posterior`, `lda-transform`
- `*->str` / `*-print` for every result type

## Install

```turmeric
:spices #map{"stats-multivariate" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                                       :ref    "stats-multivariate-v0.1.0"
                                       :subdir "spices/stats-multivariate"}}
```

It depends on `tur-linalg` (v0.2.0 or later) and `tur-stats`.

## Quick start

```turmeric
(import linalg/mat :refer [mat-of mat-free])
(import multivariate/pca :refer [pca pca-opts pca-free pca-var-explained])
(import multivariate/fmt :refer [pca-print])

(defn main [] : int
  (let [x (mat-of 4 2 1.0 2.0 2.0 3.9 3.0 6.1 4.0 8.0)
        r (pca x (pca-opts true false 0))]
    (pca-print r)
    (println (pca-var-explained r 1))
    (pca-free r)
    (mat-free x)
    0))
```

## Modules

| Module | Description |
|--------|-------------|
| `multivariate/cov` | covariance / correlation matrices, Mahalanobis distance, `mv-pinv` |
| `multivariate/pca` | principal component analysis via the SVD |
| `multivariate/factor` | exploratory factor analysis (principal axis factoring) |
| `multivariate/regress` | multivariate OLS |
| `multivariate/test` | Hotelling's T^2 (one and two sample), one-way MANOVA |
| `multivariate/lda` | linear discriminant analysis |
| `multivariate/fmt` | printing for every result type |

## Limits in v0.1

Factor analysis is unrotated principal axis factoring only (no varimax, no
maximum likelihood); MANOVA is one-way; LDA is linear only (no QDA).

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/multivariate-guide.html)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/stats-multivariate>
