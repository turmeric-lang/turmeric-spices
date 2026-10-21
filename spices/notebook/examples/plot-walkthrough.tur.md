# Plot Walkthrough

This notebook demonstrates the `tur-plot` spice from inside a notebook cell.
The workflow has two steps: write a PNG to disk with `plot-write-png`, then
announce the path with `image-hook-record-path` so the notebook displays it
inline (TUI, HTML, or markdown render).

---

## Function plot

Plot y = x^2 over [-2, 2]. The `function` renderer takes a typed callback
(a `(fn [float] float)`), a domain, a sample count, a line style, and a
label.

> This cell is not evaluated (`eval=false`): `function` calls its callback
> from plot's compiled code, and a cell's `defn` is an interpreter closure
> that compiled code cannot call. The cell runs as written in a compiled
> program.

```turmeric {eval=false}
(import plot/core  :refer [plot-write-png])
(import plot/line  :refer [function])
(import plot/decor :refer [axes tick-grid])
(import plot/style :refer [default-line-style default-plot-opts])
(import notebook/image :refer [image-hook-record-path])

(defn quadratic [x :float] :float (* x x))

(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (function quadratic -2.0 2.0 128
                    (default-line-style) "x^2"))
  (default-plot-opts)
  "/tmp/nb-plot-quadratic.png")

(unsafe (image-hook-record-path "/tmp/nb-plot-quadratic.png"))
```

---

## Scatter plot

A scatter plot takes its data as a Vec of `Point`s, the (x, y) record from
`plot/data`.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/point :refer [points])
(import plot/decor :refer [axes])
(import plot/style :refer [default-point-style default-plot-opts])
(import plot/data  :refer [Point])
(import notebook/image :refer [image-hook-record-path])

(def data
  (vec-of (Point 0.0 0.1) (Point 1.0 0.4) (Point 1.5 0.5)
          (Point 2.0 0.9) (Point 2.5 1.0) (Point 3.0 1.1)))

(plot-write-png
  (vec-of (axes)
          (points data (default-point-style) "scatter"))
  (default-plot-opts)
  "/tmp/nb-plot-scatter.png")

(unsafe (image-hook-record-path "/tmp/nb-plot-scatter.png"))
```

---

## Histogram

A discrete histogram renders categorical bar heights. Each bar is a `Bar`:
its category label and its height.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/area  :refer [discrete-histogram])
(import plot/decor :refer [axes])
(import plot/style :refer [default-fill-style default-plot-opts])
(import plot/data  :refer [Bar])
(import notebook/image :refer [image-hook-record-path])

(def bars
  (vec-of (Bar "A" 3.0) (Bar "B" 5.0) (Bar "C" 2.0) (Bar "D" 7.0)))

(plot-write-png
  (vec-of (axes)
          (discrete-histogram bars (default-fill-style) false "counts"))
  (default-plot-opts)
  "/tmp/nb-plot-histogram.png")

(unsafe (image-hook-record-path "/tmp/nb-plot-histogram.png"))
```

---

## Density estimate

The `density` renderer takes its samples as a `(Vec float)` and draws a
kernel density estimate. Here we generate 200 samples from N(0, 1) using
`tur-stats`; `rnorm` returns a column, so the cell copies it into a Vec
first.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/line  :refer [density])
(import plot/decor :refer [axes tick-grid])
(import plot/style :refer [default-line-style default-plot-opts])
(import stats/rng  :refer [rng-make])
(import stats/dist :refer [rnorm])
(import frame/column :refer [column-length column-float64-at])
(import notebook/image :refer [image-hook-record-path])

(def rng (rng-make 42 0))
(def samples (rnorm rng 200 0.0 1.0))

;; Column -> Vec of its floats, in order.
(defn col->floats [c : int i : int n : int out : (Vec float)] : (Vec float)
  (if (>= i n)
    out
    (do (vec-push! out (column-float64-at c i))
        (col->floats c (+ i 1) n out))))

(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (density (col->floats samples 0 (column-length samples) (:: (vec-new) (Vec float)))
                   0.0 128 (default-line-style) "density"))
  (default-plot-opts)
  "/tmp/nb-plot-density.png")

(unsafe (image-hook-record-path "/tmp/nb-plot-density.png"))
```

---

## The two-call pattern

Every plot cell above follows the same pattern:

1. **`plot-write-png`** writes a PNG file to disk. It returns a result
   you can check, but the file is the output.
2. **`image-hook-record-path`** prints the `__NB_IMG__:` marker to stdout,
   which the notebook session intercepts. The TUI displays the image
   inline (Kitty / iTerm2 / sixel / text fallback); the HTML exporter
   embeds it as a base64 data URL.

Without the second call, the PNG exists on disk but the notebook does not
know about it. Without the first call, there is no PNG to display.
