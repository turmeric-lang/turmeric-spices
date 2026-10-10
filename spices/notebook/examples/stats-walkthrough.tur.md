# Statistics Walkthrough

This notebook demonstrates the `tur-stats` spice: distribution functions,
summary statistics, hypothesis testing, and OLS regression.

---

## Distribution functions

The `d*` / `p*` / `q*` / `r*` naming convention matches R: density, CDF,
quantile, and random draws.

```turmeric
(import stats/dist :refer [dnorm pnorm qnorm])

;; Normal PDF at 0 (the peak): 1/sqrt(2*pi) ~ 0.3989
(println (dnorm 0.0 0.0 1.0))

;; Normal CDF at 1.96 ~ 0.975
(println (pnorm 1.96 0.0 1.0))

;; 97.5th percentile of N(0,1) ~ 1.96
(println (qnorm 0.975 0.0 1.0))
```

---

## Random samples

`rng-make` creates a deterministic RNG from a seed. `rnorm` draws n
samples from a normal distribution and returns a frame column handle.

```turmeric
(import stats/rng  :refer [rng-make])
(import stats/dist :refer [rnorm])

(def rng (rng-make 42 0))
(def samples (rnorm rng 1000 0.0 1.0))
```

---

## Summary statistics

`col-mean`, `col-sd`, `col-median` etc. operate on frame column handles.
The `rnorm` result above is a column, so we can pass it directly.

```turmeric
(import stats/summary :refer [col-mean col-sd col-median col-min col-max])

(println (col-mean samples))    ;; mean
(println (col-sd samples))      ;; sd
(println (col-median samples))  ;; median
(println (col-min samples))     ;; min
(println (col-max samples))     ;; max
```

---

## Two-sample t-test

`t-test-2samp` takes two column handles, a pooled-variance flag, an
alternative tag (`alt-two-sided`, `alt-less`, `alt-greater`), and a
confidence level. It returns a test result you can print with `print-test`.

```turmeric
(import stats/rng  :refer [rng-make])
(import stats/dist :refer [rnorm])
(import stats/test :refer [t-test-2samp alt-two-sided])
(import stats/fmt  :refer [print-test])

(def rng1 (rng-make 1 0))
(def rng2 (rng-make 2 0))
(def group-a (rnorm rng1 50 10.0 2.0))
(def group-b (rnorm rng2 50 12.0 2.0))

;; Welch's t-test (unpooled, two-sided)
(print-test (t-test-2samp group-a group-b 0 (alt-two-sided) 0.95))
```

---

## OLS regression

`ols-frame` fits an ordinary least-squares regression on a frame. Pass
the frame, the response column name, a list of predictor column names,
and an intercept flag. `print-fit` prints the coefficients and diagnostics.

> This cell is not evaluated (`eval=false`): stats' `ols-frame` reads frame
> handles with a layout frame no longer uses and crashes, compiled or not --
> turmeric's `docs/reported/stats-frame-interop-reads-a-stale-frame-layout.md`.

```turmeric {eval=false}
(import frame/csv   :refer [read-csv-string])
(import frame/frame  :refer [frame-column])
(import stats/regress :refer [ols-frame])
(import stats/fmt    :refer [print-fit])

;; A small CSV with x and y columns
(def csv "x,y\n1,2.1\n2,3.9\n3,6.2\n4,8.1\n5,9.8\n")
(def df (read-csv-string csv 0 0 1 0 ""))

;; Fit y ~ x with intercept
(def fit (ols-frame df "y" (cons (:: "x" :int) 0) 1))
(print-fit fit)
```

The `print-fit` output shows the coefficient estimates, standard errors,
t-statistics, p-values, and R-squared. For the data above, the fit
should recover an intercept near 0 and a slope near 2.
