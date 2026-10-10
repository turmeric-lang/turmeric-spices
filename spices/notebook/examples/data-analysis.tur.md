# Data Analysis Walkthrough

This notebook ties together `tur-frame`, `tur-stats`, and `tur-plot` in a
single workflow: load a CSV, compute summary statistics, fit a regression,
plot the data and the fit, and export to HTML.

This is the workflow that justifies the notebook format: each step is a
cell, the prose explains the analysis, and the final HTML export is a
shareable report.

---

## 1. Load the data

We use `read-csv-string` so the example is self-contained. In a real
notebook, use `read-csv` with a file path.

```turmeric
(import frame/csv   :refer [read-csv-string])
(import frame/frame  :refer [frame-nrows frame-ncols frame-head])
(import frame/print  :refer [print-frame])

(def csv "x,y\n1,2.1\n2,3.9\n3,6.2\n4,8.1\n5,9.8\n6,11.7\n7,14.2\n8,15.9\n9,18.1\n10,20.2\n")
(def df (read-csv-string csv 0 0 1 0 ""))

(println (frame-nrows df))   ;; rows
(println (frame-ncols df))   ;; columns
(print-frame (frame-head df 5))
```

---

## 2. Summary statistics

Extract the y column and compute summary statistics.

```turmeric
(import frame/frame   :refer [frame-column])
(import stats/summary :refer [col-mean col-sd col-median col-min col-max])

(def y-col (frame-column df "y"))

(println (col-mean y-col))    ;; mean
(println (col-sd y-col))      ;; sd
(println (col-median y-col))  ;; median
(println (col-min y-col))     ;; min
(println (col-max y-col))     ;; max
```

---

## 3. Fit a regression

Fit y ~ x with an intercept using `ols-frame`. The result includes
coefficient estimates, standard errors, and R-squared.

```turmeric
(import stats/regress :refer [ols-frame])
(import stats/fmt    :refer [print-fit])

;; ols-frame returns a result -- (0 . (fit)) on success; head/tail unwrap it.
(def fit (head (tail (ols-frame df "y" (cons (:: "x" :int) 0) 1))))
(print-fit fit)
```

---

## 4. Plot the data and the fit

Plot the observed data as points and the fitted line through two points.
The two-call pattern (write PNG, then record path) makes the image
visible in the TUI and in HTML export.

```turmeric
(import frame/frame  :refer [frame-column])
(import frame/column :refer [column-length column-int64-at column-float64-at])
(import plot/core    :refer [plot-write-png])
(import plot/point   :refer [points])
(import plot/line    :refer [lines])
(import plot/decor   :refer [axes tick-grid])
(import plot/style   :refer [default-line-style default-point-style default-plot-opts])
(import notebook/image :refer [image-hook-record-path])

;; Extract x and y columns from the frame
(def x-col (frame-column df "x"))
(def y-col (frame-column df "y"))
(def n (column-length x-col))

;; Build a list of (x, y) pairs for the scatter plot
(load "stdlib/bits.tur")
(load "stdlib/math.tur")

;; One (x, y) point: a cons cell of the two floats' bits.
(defn xy [x :float y :float] : int (cons (float->bits x) (float->bits y)))
(defn lst [v : int n : int] : int (cons v n))

(defn build-pairs [i : int acc : int] : int
  (if (>= i n)
    acc
    (build-pairs (+ i 1)
      ;; x was read as int64 (the CSV writes 1, 2, ...), y as float64
      (lst (xy (int->float (column-int64-at x-col i))
               (column-float64-at y-col i)) acc))))

(def scatter-data (build-pairs 0 0))

;; Fitted line: y = intercept + slope * x, intercept ~ 0.05, slope ~ 2.00.
;; A straight line needs only its two end points.
(defn fit-line [x :float] :float
  (+ 0.05 (* 2.00 x)))
(def fit-data (lst (xy 0.0 (fit-line 0.0)) (lst (xy 10.0 (fit-line 10.0)) 0)))

(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (points scatter-data (default-point-style) "observed")
          (lines fit-data (default-line-style) "fitted"))
  (default-plot-opts)
  "/tmp/nb-data-analysis.png")

(unsafe (image-hook-record-path "/tmp/nb-data-analysis.png"))
```

---

## 5. Export to HTML

To share this analysis, export the notebook to a standalone HTML page:

```sh
tur nb export html data-analysis.tur.md
```

The HTML export embeds the plot as a base64 data URL, so the resulting
file is self-contained and can be shared by email or hosted on any
static server.
