# tur-ecs-raylib

Raylib 5.5 companion to [`tur-ecs`](https://github.com/turmeric-lang/turmeric-spices/blob/main/spices/ecs/README.md): standard 2D
components, integration / rendering systems, and a `with-game-loop`
macro for ECS-driven games.

This is **E3** in the ECS spice plan
([`docs/archive/ecs-spice-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/ecs-spice-plan.md)).

## Status

**E3 -- raylib companion.** Shipped 2026-06-11.

### What's in

- `ecs-raylib/components` -- the standard 2D component set:
  `Pos2`, `Vel2`, `Rot`, `Radius`, `Color`. Each is a `defopaque`
  around `:int` (the dense-storage carrier); helpers (`pos2-make`,
  `pos2-x`, `pos2-y`, `vel2-make`, `pos2-add!`, `radius-make`,
  `radius-value`, `rot-make`, `rot-value`) pack and unpack the
  fixed-point quantisation. Sub-pixel precision is 1/1000th of a
  world unit; range is roughly +/- 2.1M units per axis.
- `ecs-raylib/systems` -- raylib-free integration systems:
  `integrate-2d` (a macro that emits a typed `defn` advancing every
  `(Pos2, Vel2)` entity by `Pos2 += Vel2 * dt`).
- `ecs-raylib/render` -- raylib-rendering systems:
  `render-circles` (a macro that emits a typed `defn` drawing every
  `(Pos2, Radius, Color)` entity as a filled circle). Split from
  `systems` so headless tests of integration math compile without
  needing `<raylib.h>` on the include path.
- `ecs-raylib/loop` -- `with-game-loop` macro wrapping the standard
  raylib boilerplate (init-window / window-should-close /
  begin-drawing / end-drawing / close-window) around a user body
  that sees `dt` (the frame delta-time) in scope.

### Quick start

```turmeric
(defmodule my-game (export)

(import ecs/entity   :refer [entity-index])
(import ecs/storage  :refer [dense-new dense-set! dense-get])
(import ecs/world    :refer [defcomponent defworld world-alloc-entity!])
(import ecs/query    :refer [for-each])
(import ecs-raylib/components :refer [Pos2 Vel2 Radius
                                       pos2-make vel2-make radius-make])
(import ecs-raylib/systems    :refer [integrate-2d])
(import ecs-raylib/render     :refer [render-circles])
(import ecs-raylib/loop       :refer [with-game-loop])
(import raylib/types          :refer [Color])
(import raylib/color          :refer [red])

(defcomponent Pos2)
(defcomponent Vel2)
(defcomponent Radius)
(defcomponent Color)
(defworld Scene [Pos2 Vel2 Radius Color])

(integrate-2d  integrate Scene)
(render-circles render   Scene)

(defn main [] : int
  (let [w (make-struct Scene (vec-new)
            (dense-new) (dense-new) (dense-new) (dense-new))]
    (let [e (world-alloc-entity! (.gens w))
          i (entity-index e)]
      (dense-set! (.Pos2   w) i (pos2-make 400.0 300.0))
      (dense-set! (.Vel2   w) i (vel2-make  120.0  80.0))
      (dense-set! (.Radius w) i (radius-make 30.0))
      (dense-set! (.Color  w) i (red)))
    (with-game-loop w "my game" 800 600 60
      (do (integrate w dt) (render w)))
    0))

) ;; end defmodule
```

```sweet-exp
#lang sweet-exp
defmodule my-game (export)
  (import ecs/entity   :refer [entity-index])
  (import ecs/storage  :refer [dense-new dense-set! dense-get])
  (import ecs/world    :refer [defcomponent defworld world-alloc-entity!])
  (import ecs/query    :refer [for-each])
  (import ecs-raylib/components :refer [Pos2 Vel2 Radius
                                         pos2-make vel2-make radius-make])
  (import ecs-raylib/systems    :refer [integrate-2d])
  (import ecs-raylib/render     :refer [render-circles])
  (import ecs-raylib/loop       :refer [with-game-loop])
  (import raylib/types          :refer [Color])
  (import raylib/color          :refer [red])

  defcomponent Pos2
  defcomponent Vel2
  defcomponent Radius
  defcomponent Color
  defworld Scene [Pos2 Vel2 Radius Color]

  integrate-2d   integrate Scene
  render-circles render    Scene

  defn main [] : int
    let [w make-struct(Scene vec-new()
                       dense-new() dense-new() dense-new() dense-new())]
      let [e world-alloc-entity!(.gens(w))
           i entity-index(e)]
        dense-set!(.Pos2(w)   i pos2-make(400.0 300.0))
        dense-set!(.Vel2(w)   i vel2-make(120.0 80.0))
        dense-set!(.Radius(w) i radius-make(30.0))
        dense-set!(.Color(w)  i red())
      with-game-loop w "my game" 800 600 60
        do
          integrate(w dt)
          render(w)
      0
```

## Install

```turmeric no-check
:spices {
  "ecs-raylib" {:url    "https://github.com/turmeric-lang/turmeric-spices"
                :ref    "ecs-raylib-v0.1.0"
                :subdir "spices/ecs-raylib"}
}
```

Local development: depend on `../ecs` and `../raylib` via
`:path` instead of `:url`. The spice ships `build.tur` configured
this way for the in-repo workflow.

## Smoke / regression tests

```sh
tur run     tests/component-smoke.tur       # E3 carrier round-trip (5/5)
tur run     tests/integrate-2d-system.tur   # E3 integrate end-to-end (0)
tur emit-c  tests/render-elab.tur           # E3 render-circles elaborates
tur emit-c  tests/demo-bouncing-balls.tur   # E3 demo elaborates + emits
```

`tests/demo-bouncing-balls.tur` requires `raylib 5.5` on the
cmake-deps path to **run**; it elaborates and emits cleanly without
raylib (which is what the test suite exercises in CI).

## Demo

[`tests/demo-bouncing-balls.tur`](tests/demo-bouncing-balls.tur)
is the canonical worked example -- five circles bouncing in an
800x600 window. It uses the stock `integrate-2d` /
`render-circles` systems plus a hand-written `bounce-walls`
velocity-flip system to demonstrate the typical mix.

## Where to read about the ECS model

- [`docs/guides/ecs-guide.md`](../../../turmeric/docs/guides/ecs-guide.md) --
  end-to-end walkthrough of components, queries, systems, and the
  game loop.
- [`docs/archive/ecs-spice-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/ecs-spice-plan.md) --
  the long-form plan and v2 roadmap.
- [`tur-ecs`](https://github.com/turmeric-lang/turmeric-spices/blob/main/spices/ecs/README.md) -- the upstream
  ECS spice's release notes and known limitations.

## License

MIT.
