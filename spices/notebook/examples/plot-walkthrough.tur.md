# Plot Walkthrough

This notebook demonstrates the `tur-plot` spice from inside a notebook cell.
The workflow has two steps: write a PNG to disk with `plot-write-png`, then
announce the path with `image-hook-record-path` so the notebook displays it
inline (TUI, HTML, or markdown render).

---

## Function plot

Plot y = x^2 over [-2, 2]. The `function` renderer takes a typed callback
(`defn` with `:float` argument and return), a domain, a sample count, a
line style, and a label.

```turmeric
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

(image-hook-record-path "/tmp/nb-plot-quadratic.png")
```

---

## Scatter plot

A scatter plot needs a list of (x, y) pairs. Each pair is a cons cell whose
head is the x value and tail is the y value (both stored as raw float bits).
The list itself is a cons list of those pair cells.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/point :refer [points])
(import plot/decor :refer [axes])
(import plot/style :refer [default-point-style default-plot-opts])
(import notebook/image :refer [image-hook-record-path])

(defn xy [x :float y :float] : int
  ```c
  typedef struct { int64_t head; int64_t tail; } Cons;
  Cons *p = malloc(sizeof(*p));
  union { double d; int64_t i; } ux, uy; ux.d = x; uy.d = y;
  p->head = ux.i; p->tail = uy.i;
  return (int64_t)(intptr_t)p;
  ```)

(defn lst [v : int n : int] : int
  ```c
  typedef struct { int64_t head; int64_t tail; } Cons;
  Cons *c = malloc(sizeof(*c));
  c->head = v; c->tail = n;
  return (int64_t)(intptr_t)c;
  ```)

(def data
  (lst (xy 0.0 0.1)
    (lst (xy 1.0 0.4)
      (lst (xy 1.5 0.5)
        (lst (xy 2.0 0.9)
          (lst (xy 2.5 1.0)
            (lst (xy 3.0 1.1) 0)))))))

(plot-write-png
  (vec-of (axes)
          (points data (default-point-style) "scatter"))
  (default-plot-opts)
  "/tmp/nb-plot-scatter.png")

(image-hook-record-path "/tmp/nb-plot-scatter.png")
```

---

## Histogram

A discrete histogram renders categorical bar heights. Each bar is a cons
cell whose head is the category label (cstr) and tail is the height (float).

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/area  :refer [discrete-histogram])
(import plot/decor :refer [axes])
(import plot/style :refer [default-fill-style default-plot-opts])
(import notebook/image :refer [image-hook-record-path])

(defn cat [label : cstr height : float] : int
  ```c
  typedef struct { int64_t head; int64_t tail; } Cons;
  Cons *c = malloc(sizeof(*c));
  union { double d; int64_t i; } u; u.d = height;
  c->head = (int64_t)(intptr_t)label; c->tail = u.i;
  return (int64_t)(intptr_t)c;
  ```)

(defn lst [v : int n : int] : int
  ```c
  typedef struct { int64_t head; int64_t tail; } Cons;
  Cons *c = malloc(sizeof(*c));
  c->head = v; c->tail = n;
  return (int64_t)(intptr_t)c;
  ```)

(def bars
  (lst (cat "A" 3.0)
    (lst (cat "B" 5.0)
      (lst (cat "C" 2.0)
        (lst (cat "D" 7.0) 0)))))

(plot-write-png
  (vec-of (axes)
          (discrete-histogram bars (default-fill-style) 0 "counts"))
  (default-plot-opts)
  "/tmp/nb-plot-histogram.png")

(image-hook-record-path "/tmp/nb-plot-histogram.png")
```

---

## Density estimate

The `density` renderer takes a list of float samples and draws a kernel
density estimate. Here we generate 200 samples from N(0, 1) using
`tur-stats` and pass them directly.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/line  :refer [density])
(import plot/decor :refer [axes tick-grid])
(import plot/style :refer [default-line-style default-plot-opts])
(import stats/rng  :refer [rng-make])
(import stats/dist :refer [rnorm])
(import notebook/image :refer [image-hook-record-path])

(def rng (rng-make 42 0))
(def samples (rnorm rng 200 0.0 1.0))

(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (density samples 0.0 128
                   (default-line-style) "density"))
  (default-plot-opts)
  "/tmp/nb-plot-density.png")

(image-hook-record-path "/tmp/nb-plot-density.png")
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
