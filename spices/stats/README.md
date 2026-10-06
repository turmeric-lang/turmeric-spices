# tur-stats

Statistical analysis on dataframes for Turmeric: summary statistics,
distributions, hypothesis tests, OLS regression, and resampling.

## Overview

`tur-stats` is a Tier 1 spice (inline-C only). It builds on `tur-frame` to
provide a comprehensive statistical toolkit for data analysis in Turmeric.

The spice covers:
* **Summary statistics**: count, sum, mean, median, mode, variance, standard
deviation, min, max, range, quantiles, IQR, skewness, kurtosis
* **Covariance and correlation**: covariance, Pearson correlation,
Spearman rank correlation, correlation matrices
* **Probability distributions**: normal, t, chi-square, F, uniform, binomial,
Poisson, exponential, beta, gamma — with PDF, CDF, quantile, and random
sampling functions
* **Hypothesis tests**: t-tests (1-sample, 2-sample, paired), ANOVA,
chi-square goodness-of-fit, chi-square contingency, variance tests, correlation
tests, Mann-Whitney U, Wilcoxon signed-rank, Kolmogorov-Smirnov
* **OLS regression**: linear regression with diagnostics
* **Resampling**: bootstrap, permutation tests, train-test split, cross-validation
* **Random number generation**: various RNG utilities including shuffling
and sampling

## Install

```turmeric no-check
:spices {
  "stats" {:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "stats-v0.1.0"
           :subdir "spices/stats"}
}
```

## Quick start

```turmeric
(import frame/csv :refer [read-csv-string])
(import stats/summary :refer [describe])

;; Load data and get summary statistics
(let [f (read-csv-string "a,b,c\n1,2,3\n4,5,6\n7,8,9\n" 0 0 1 0 "")]
  (describe f))
```

```sweet-exp
#lang sweet-exp
import frame/csv :refer [read-csv-string]
import stats/summary :refer [describe]

;; Load data and get summary statistics
let [f read-csv-string("a,b,c\n1,2,3\n4,5,6\n7,8,9\n" 0 0 1 0 "")]
  describe(f)
```

```turmeric
(import stats/dist :refer [dnorm pnorm qnorm rnorm])

;; Work with normal distribution
(println (dnorm 0.0 0.0 1.0))  ; PDF at 0
(println (pnorm 1.96 0.0 1.0)) ; CDF at 1.96
(println (qnorm 0.975 0.0 1.0)) ; 97.5th percentile
(println (rnorm 0.0 1.0))    ; random sample
```

```sweet-exp
import stats/dist :refer [dnorm pnorm qnorm rnorm]

;; Work with normal distribution
println(dnorm(0.0 0.0 1.0))
println(pnorm(1.96 0.0 1.0))
println(qnorm(0.975 0.0 1.0))
println(rnorm(0.0 1.0))
```

```turmeric
(import stats/test :refer [t-test-2samp])
(import frame/frame :refer [frame])

;; Two-sample t-test
(let [a (frame "x" (list 1.0 2.0 3.0))
      b (frame "x" (list 4.0 5.0 6.0))
      r (t-test-2samp a b)]
  (println r))
```

```sweet-exp
import stats/test :refer [t-test-2samp]
import frame/frame :refer [frame]

;; Two-sample t-test
let [a frame("x" list(1.0 2.0 3.0))
     b frame("x" list(4.0 5.0 6.0))
     r t-test-2samp(a b)]
  println(r)
```

## See also

- [Guide](https://turmeric-lang.com/docs/html/guides/stats-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/stats>
