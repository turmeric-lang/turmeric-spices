# Changelog

## Unreleased

### Changed (breaking)

- Every renderer kind is its own type with a `Renderer` instance
  (plot Renderer typeclass plan, P4): `LinesR`, `FunctionR`, `ParametricR`,
  `PolarR`, `InverseR`, `DensityR` (plot/line); `PointsR`, `ErrorBarsR`,
  `VectorFieldR`, `ArrowsR` (plot/point); `FunctionIntervalR`,
  `LinesIntervalR`, `ParametricIntervalR`, `PolarIntervalR` (plot/interval);
  `RectanglesR`, `AreaHistogramR`, `DiscreteHistogramR`, `StackedHistogramR`
  (plot/area); `IsolineR`, `ContoursR`, `ContourIntervalsR`, `ColorFieldR`
  (plot/contour); and in plot/decor the `Decoration` sum type (`XAxis`,
  `YAxis`, `Axes`, `XTickLines`, `YTickLines`, `TickGrid`) plus `HRuleR`,
  `VRuleR` and `LabelR`. Each carries its own named, typed fields instead of
  the shared `data1..5` / `f1..6` / `i1..4` slot bag.
- Every public constructor returns an `AnyRenderer` -- the renderer boxed
  with its instance, `(exists [a] [(Renderer a)] a)` -- instead of an `int`
  handle, and `plot`, `plot-write-png`, `plot-into-canvas`, `render` and
  `Backend`'s `render-to` take a `(Vec AnyRenderer)`. A `vec-of` over the
  constructors needs no change; a Vec annotated `(Vec int)` now says
  `(Vec AnyRenderer)`, and one built with `vec-new` is ascribed
  `(:: (vec-new) (Vec AnyRenderer))`.
- Curve callbacks are function values of a declared type: `function`,
  `polar` and `inverse` take a `(fn [float] float)`, `parametric` a
  `(fn [float] int)` returning a point, `vector-field` a
  `(fn [float float] int)`, the contour kinds a `(fn [float float] float)`
  and `color-field` a `(fn [float float] int)`, and the interval kinds two
  of their curve's type. A `fn` literal -- capturing or not -- works where a
  named `defn` did, and a callback of the wrong type is a compile error
  rather than a crash. The draw pass calls them through the closure ABI
  (`TUR_APPLY1_T` / `TUR_APPLY2_T`).
- The plot's data extent is the union of the renderers' `bounds`, dispatched
  through each one's instance. The draw pass's per-kind bounds `switch`
  (`__renderer-bbox`, C symbol `plot_renderer_bbox`) is gone, and
  `renderer-bbox` takes an `AnyRenderer`.
- `lines-r`, `points-r` and `label-r` are removed (build the struct with
  `make-struct`), and `plot-anyrenderers` is deprecated: `plot` takes the
  same argument.

### Added

- `Backend` typeclass in `plot/core` (U2 typeclass collapse): the three
  output sinks now share one `render-to` method with one instance each --
  `CanvasBackend` (draw into an existing canvas + viewport),
  `SurfaceBackend` (allocate a fresh surface, optional explicit
  dimensions), and `PngBackend` (render + write a PNG). Adding a new sink
  (e.g. notebook-inline) is now a new instance rather than a new
  top-level function. `plot`, `plot-write-png`, and `plot-into-canvas`
  remain as thin wrappers that route through `render-to`, so existing
  call sites are unchanged.
- `render` in `plot/core`: a single entry point generic over `Backend`
  (`(defn render [^Backend B b : B  renderers : (Vec AnyRenderer)  opts : int] ...)`),
  so plot-building code can dispatch to any sink without knowing the
  concrete backend type -- the "plot DSL becomes generic in Backend"
  the uplift plan called for.

- `errors/run.sh` and three compile-fail fixtures: a type with no `Renderer`
  instance cannot be boxed, `plot` rejects a `(Vec int)` of handles, and a
  callback of the wrong function type is rejected.

### Fixed

- `arrows` bounds read each arrow's second cons cell -- a pointer -- as the y
  coordinate, so an arrows plot's y range came out of the heap layout and
  changed from run to run. `ArrowsR`'s `bounds` spans both endpoints of each
  arrow.
- The slot-bag handle each constructor allocated was never freed. Handles
  now live only for one draw: `plot-into-canvas` lowers the renderers,
  draws, and frees them.
- `plot/core`'s hand-written `<plutovg.h>` stand-ins (prototypes, typedefs,
  `PLUTOVG_TEXT_ENCODING_UTF8`) are guarded by the header's own `PLUTOVG_H`.
  A project build (`tur build --shared --bundle-deps`, the image `tur repl`
  and notebooks load) puts the real header on the include path and hoists
  it, and the stand-ins then conflicted with its prototypes.
- x-axis tick labels showed y values. The label buffers were local to the
  blocks that filled them; `x_labels` outlived its block and the y block
  reused the storage, so every x tick but the last read a y label.
- A discrete or stacked histogram's category labels were garbage: the tick
  loop printed each bar's address as its label instead of the bar's head.
- `__plot-sample-adaptive` is now pinned to the stable C symbol
  `plot_sample_adaptive` via `export-as`. turmeric's injective identifier
  mangler encodes the `__`-prefixed, kebab-cased name differently from the
  legacy fold the inline-C call site hard-coded, which broke linking of
  the sampled-curve renderers against tip-of-main turmeric.

### Changed

- `AnyRenderer` fold internals: paid down the soft-blocker workarounds the
  `bounds`/`to-legacy` folds carried, now that turmeric main fixed the
  self-recursive-`defn` carrier typing and the pass-by-ptr struct-param
  return. `bbox-union` returns an empty input's peer box directly instead
  of rebuilding it field-by-field; `__renderers-bbox-go` threads a real
  `BBox` accumulator through `bbox-union` (the raw `{x0,x1,y0,y1;valid}`
  carrier and its `__rbb-new-empty` / `__rbb-union-corners!` helpers are
  removed); `__any-to-legacy-go` drops its recursive-call `(:: ... (Vec int))`
  ascription, and `anyrenderers->legacy` drops its `(:: (vec-new) (Vec int))`
  seed ascription now that turmeric #463 unifies a fresh `vec-new` against a
  concrete `(Vec T)` argument. No public-surface or pixel change. One residual
  codegen gap keeps the two folds as top-level `defn`s rather than local
  `letrec` closures: a `letrec` closure that captures the existential renderer
  vec and `open`s it mis-emits the captured binding in C — documented as W3 in
  `docs/spice-uplift-residual-soft-blockers-2026-06-20.md` for a turmeric report.

## 0.3.0

### Added

- Adaptive subdivision sampler in `plot/core` (`__plot-sample-adaptive`).
  The sampled-curve renderers (`function`, `parametric`, `polar`,
  `inverse`) now bisect intervals where adjacent screen-space segments
  turn sharply or cross a NaN boundary, up to a fixed depth cap. The
  `samples` argument keeps acting as the uniform baseline; smooth curves
  pay the baseline cost, sharp turns / asymptotes pick up extra samples
  without raising it globally. `lines` is unchanged.
- Pixel-readback helpers (`__surface-width`, `__surface-height`,
  `__surface-pixel-byte`, `__pixel-is-background?`,
  `__count-non-background`) so callers and tests can inspect the
  rendered surface without round-tripping through PNG.

### Fixed

- The "auto y bound" NaN sentinels in `plot/line/function`,
  `plot/interval/function-interval`, and `plot/decor/{hrule,vrule}-styled`
  used `(/ 0.0 0.0)`, which the current Turmeric runtime aborts on. The
  sentinels are now inline-C `0.0/0.0` helpers (no runtime check).
- `plot/core` now uses local `__is-ok?` / `__ok-val` / `__err-val`
  helpers for its `:ptr<void>` result envelopes, restoring type-checking
  under the current compiler.

### Tests

- Every renderer family's test now actually runs (`describe` is invoked
  from `main` rather than at module top level) and the assertions
  inspect rendered surface pixels in addition to the PNG file size.

## 0.2.0

### Breaking changes

- `plot`, `plot-into-canvas`, and `plot-write-png` now take a
  `Vec[Renderer]` as their `renderers` argument instead of a cons list
  terminated by `0`. Build the argument with `(vec-of ...)`, or with
  `vec-new` + `vec-push!` for dynamic construction.

  Before:

  ```turmeric
  (plot-write-png
    (list (tick-grid) (axes)
          (function (fn [x :float] :float (* x x)) -2.0 2.0 128
                    (default-line-style) "x^2"))
    (default-plot-opts)
    "quadratic.png")
  ```

  After:

  ```turmeric
  (plot-write-png
    (vec-of (tick-grid) (axes)
            (function (fn [x :float] :float (* x x)) -2.0 2.0 128
                      (default-line-style) "x^2"))
    (default-plot-opts)
    "quadratic.png")
  ```

  Inner renderer payload data (lines/points/etc. sample lists) is
  unchanged -- still cons-shaped.

  Callers that still pass a cons list will get a runtime crash when the
  C inline-loop dereferences `Vec.data`, not a type error, because both
  types are carried as `:int` at the type-system level.

## 0.1.0

- Initial release.
