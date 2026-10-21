# tur-plot

2D data visualization for Turmeric: functions, points, intervals, histograms,
contours, legends, and PNG output via plutovg.

## Overview

`tur-plot` is a Tier 1 spice implemented in Turmeric with thin inline-C helpers
for sampling and rendering. It draws into an off-screen plutovg surface and
returns that surface handle to the caller, or writes directly to PNG.

It is designed to pair naturally with `tur-plutovg`: use `plot` when you want a
finished chart surface, or `plot-into-canvas` when you need to compose several
plots into a larger image.

## Install

```turmeric no-check
:spices {
  "plot" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "plot-v0.3.0"
          :subdir "spices/plot"}
}
```

## Quick start

Each renderer constructor (`function`, `lines`, `axes`, ...) returns an
`AnyRenderer`, and `plot-write-png` takes a `(Vec AnyRenderer)`, so a `vec-of`
over them mixes kinds freely. A sampled curve's callback is a typed function
value -- a `defn`, or a `fn` literal that may capture.

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/line  :refer [function])
(import plot/decor :refer [axes tick-grid])
(import plot/style :refer [default-line-style default-plot-opts])

(defn quadratic [x :float] :float (* x x))

(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (function quadratic
                    -2.0 2.0 128
                    (default-line-style)
                    "x^2"))
  (default-plot-opts)
  "quadratic.png")
```

```sweet-exp
#lang sweet-exp
import plot/core  :refer [plot-write-png]
import plot/line  :refer [function]
import plot/decor :refer [axes tick-grid]
import plot/style :refer [default-line-style default-plot-opts]

defn quadratic [x : float] : float
  *(x x)

plot-write-png
  vec-of(tick-grid()
         axes()
         function(quadratic
                  -2.0 2.0 128
                  default-line-style()
                  "x^2"))
  default-plot-opts()
  "quadratic.png"
```

`samples` (128 above) is a baseline; the renderer adaptively bisects
segments in high-curvature regions and around NaN boundaries up to a fixed
depth, so smooth curves stay cheap and sharp turns get extra samples
without forcing every caller to crank the count globally.

## Renderers are typed

Every renderer kind is its own type -- `FunctionR`, `LinesR`, `PointsR`,
`ContoursR`, the `Decoration` sum type for axes and grids, and so on -- with
named fields and an instance of the `Renderer` class:

```turmeric no-check
(defclass Renderer [a]
  (bounds [r] : BBox)       ;; the data extent the renderer needs on screen
  (to-legacy [r] : int))    ;; lowering for the C draw pass (internal)
```

The plot's extent is the union of its renderers' `bounds`. A constructor
boxes its struct with that instance as an `AnyRenderer`, which is what
lets one Vec hold every kind. Passing anything else to `plot` -- a value with
no `Renderer` instance, or a callback of the wrong function type -- is a
compile error, not a mis-rendered chart.

## See also

- [Guide](https://github.com/turmeric-lang/turmeric-spices/blob/main/spices/plot/docs/guides/plot-guide.md)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/plot>
