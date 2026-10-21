# tur-plot guide

## 1. Basic function plot

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
  "function.png")
```

Sampled-curve renderers (`function`, `parametric`, `polar`, `inverse`) take
their curve as a typed function value: `function`, `polar` and `inverse` a
`(fn [float] float)`, `parametric` a `(fn [float] Point)`. A named `defn` works, and so does a `fn` literal,
including one that captures:

```turmeric no-check
(let [k 0.5]
  (function (fn [x : float] : float (* k x x)) -2.0 2.0 128
            (default-line-style) "0.5 x^2"))
```

A callback of the wrong type is a compile error naming the type expected.

The `samples` argument (128 above) is a baseline; `function`, `parametric`,
`polar`, and `inverse` feed it to an adaptive subdivision pass that bisects
segments (up to depth 6) wherever neighboring screen-space pieces turn sharply
or cross a NaN/non-NaN boundary. Smooth curves keep the baseline cost; sharp
turns and asymptotes get extra samples without forcing every caller to
crank `samples` globally. `lines` is unaffected -- it plots its caller's
points verbatim and only breaks on NaN coordinates.

## 2. Scatter plot with error bars

Data is a Vec of typed records from `plot/data`: `Point` for points and
polylines, `ErrorBar`, `Arrow`, `Rect`, `Bar` and `Group` for the rest.

```turmeric no-check
(import plot/data :refer [Point ErrorBar])

(plot-write-png
  (vec-of (axes)
          (points (vec-of (Point 1.0 2.0) (Point 2.0 3.5) (Point 3.0 3.25))
                  (default-point-style) "observed")
          (error-bars (vec-of (ErrorBar 1.0 2.0 0.25) (ErrorBar 2.0 3.5 0.5))
                      (default-line-style) false "error"))
  (default-plot-opts)
  "scatter.png")
```

A point with a NaN coordinate breaks a `lines` polyline there and is left
out of the plot's extent. `error-bars`' last flag draws x +/- err instead of
y +/- err.

## 3. Shaded interval

Use `function-interval` to fill between two functions.

## 4. Histogram

Use `discrete-histogram` for categorical bars and `stacked-histogram` for
stacked series. The x axis is labelled with the bars' (or groups') labels --
the y axis when `horizontal` is true. Each stack layer takes one fill, in
every group, and the legend lists the layer names, one row per layer.

```turmeric no-check
(import plot/data :refer [Bar Group])

(discrete-histogram (vec-of (Bar "A" 3.0) (Bar "B" 5.0) (Bar "C" 2.0))
                    (default-fill-style) false "counts")

(stacked-histogram (vec-of (Group "Q1" (vec-of 1.0 0.5)) (Group "Q2" (vec-of 1.25 0.75)))
                   (vec-of (fill-style 0 0.6 -1 0.0) (fill-style 1 0.6 -1 0.0))
                   (vec-of "base" "extra")
                   false)
```

`area-histogram` takes pre-bucketed `Rect`s instead, and `rectangles`
draws arbitrary ones.

## 5. Contour plot

Use `contours`, `contour-intervals`, and `color-field` together for sampled
scalar fields. `contours` spaces its `n` levels evenly through the range f
takes on the `samples` x `samples` grid, leaving out the extremes (level i at
min + (max - min) * (i + 1) / (n + 1)); `isoline` draws one level you name;
`contour-intervals` bands f over [0, 1].

## 6. Multiple renderers and legend

Pass labels on data renderers and set `legend-pos` in `plot-opts` (1 top
right, 2 top left, 3 bottom right, 4 bottom left, 0 none). The legend sits on
a translucent white box sized to its rows. Each labelled renderer gets a row whose swatch is what it drew with: a stroke of
its line style, a box of its fill (edged in its outline's style for an
interval band), or its point marker -- auto colors included, so a swatch
always matches its series.

## 7. Styles

`line-style`, `point-style` and `fill-style` build a `LineStyle`,
`PointStyle` and `FillStyle`; `plot-opts` builds the `PlotOpts` every entry
point takes. Each renderer takes the kind of style it draws with -- `lines`
a `LineStyle`, `points` a `PointStyle`, the interval kinds a `FillStyle`
for the band and a `LineStyle` for its outline -- so handing one the wrong
kind is a compile error. A color of `-1` takes the next palette color
automatically. The renderers that cycle through several fills take them as a
Vec:

```turmeric no-check
(area-histogram bars (vec-of (fill-style 0 0.5 -1 0.0) (fill-style 1 0.5 -1 0.0)) "hist")
```

## 8. Renderer types

Every constructor returns an `AnyRenderer`: its own renderer struct boxed
with the struct's `Renderer` instance. `plot`, `plot-write-png`,
`plot-into-canvas` and `render` take a `(Vec AnyRenderer)`, so a `vec-of`
over constructors of different kinds is one Vec. A Vec built up with
`vec-new` needs its element type written down, because `vec-push!` does not
infer it:

```turmeric no-check
(let [rs (:: (vec-new) (Vec AnyRenderer))]
  (vec-push! rs (axes))
  (vec-push! rs (lines pts (default-line-style) "data"))
  (plot-write-png rs (default-plot-opts) "data.png"))
```

| Module | Constructors | Types |
|---|---|---|
| `plot/line` | `function` `lines` `parametric` `polar` `inverse` `density` | `FunctionR` `LinesR` `ParametricR` `PolarR` `InverseR` `DensityR` |
| `plot/point` | `points` `error-bars` `vector-field` `arrows` | `PointsR` `ErrorBarsR` `VectorFieldR` `ArrowsR` |
| `plot/interval` | `function-interval` `lines-interval` `parametric-interval` `polar-interval` | `FunctionIntervalR` `LinesIntervalR` `ParametricIntervalR` `PolarIntervalR` |
| `plot/area` | `rectangles` `area-histogram` `discrete-histogram` `stacked-histogram` | `RectanglesR` `AreaHistogramR` `DiscreteHistogramR` `StackedHistogramR` |
| `plot/contour` | `isoline` `contours` `contour-intervals` `color-field` | `IsolineR` `ContoursR` `ContourIntervalsR` `ColorFieldR` |
| `plot/decor` | `x-axis` `y-axis` `axes` `x-tick-lines` `y-tick-lines` `tick-grid` | `Decoration` (`XAxis` `YAxis` `Axes` `XTickLines` `YTickLines` `TickGrid`) |
| `plot/decor` | `hrule` `vrule` `point-label` (and `-styled` forms) | `HRuleR` `VRuleR` `LabelR` |

Each type's `bounds` is the data extent it needs; the plot is laid out over
the union of them (padded 5%, then overridden side by side by `plot-opts`).
The frame draws the axis lines and tick grids that the renderers'
`frame-marks` ask for (only the `Decoration`s ask), and labels the x axis
(the y axis when it is `categories-on-y?`) with the `category-labels` of the
first `categorical?` renderer; then every renderer's `draw` paints it, in Vec
order, clipped to the viewport, and the legend lists every renderer's
`legend-labels` -- by default its one non-empty `legend-label`.
Point lists span their points, grid kinds (`contours`, `vector-field`, ...)
their declared domain, and a sampled curve or a decoration constrains
nothing. `renderer-bbox` and `renderers-bbox` in `plot/core` report it:

```turmeric no-check
(renderers-bbox (vec-of (axes) (lines pts (default-line-style) "")))
;; => the BBox of pts; the axes add nothing
```
