# tur-ecs cross-world systems guide

This guide covers the **cross-world** ECS surface: a single system that
reads from one typed world and writes to another, with the scheduler
proving non-conflict per *(world, component)* pair. It is the post-v1
follow-up to single-world `defsystem`, implementing
[`docs/upcoming/ecs-cross-world-systems-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/ecs-cross-world-systems-plan.md)
(phases X1–X4) in the turmeric repo.

## When you want it

The shared pattern is **bulk component copy / projection between two
typed worlds, with a transformation in the middle**:

1. **Render extraction.** A `SimWorld` holds the simulation; a separate
   `RenderWorld` holds only what the renderer needs, refilled each frame
   by one extract system — so "simulate frame N+1" can overlap "render
   frame N."
2. **Client-side prediction.** A `PredictedWorld` integrated locally and
   an `AuthoritativeWorld` overwritten from server snapshots;
   reconciliation reads auth, writes predicted.
3. **Save / rewind.** Periodic copy `LiveWorld` → `SnapshotWorld` and
   back.
4. **Editor vs play mode.** Mutate an `EditorWorld` while a paused
   `PlayWorld` holds the last sim state.

## The model in one example

```turmeric
(import ecs/sized-world :refer [sized-defworld-mono])
(import ecs/xworld      :refer [sized-defcomponent-accessors-xmono])
(import ecs/xcap        :refer [XWriteCap XReadCap
                                make-write-cap make-read-cap use-cap!])
(import ecs/xsystem     :refer [XSystem make-xsystem defxsystem])
(import ecs/xstage      :refer [BoundXSystem bind-xsystem
                                XStage xstage-new xstage-add!
                                xstage-run! xstage-free!])

(defopaque Pos       :int)
(defopaque RenderPos :int)

(sized-defworld-mono SimWorld    (Static 1024) [Pos])
(sized-defcomponent-accessors-xmono SimWorld Pos)
(sized-defworld-mono RenderWorld (Static 1024) [RenderPos])
(sized-defcomponent-accessors-xmono RenderWorld RenderPos)

(def Pos-cid 0)
(def RenderPos-cid 0)

(defxsystem extract-renderables
  [sim SimWorld  ren RenderWorld]
  :reads-from  sim [Pos]
  :writes-to   sim []
  :reads-from  ren []
  :writes-to   ren [RenderPos]
  (set-RenderPos! ren-RenderPos-write-cap ren 0
    (:: (:: (get-Pos sim-Pos-read-cap sim 0) :int) :RenderPos)))
```

Two world bindings, four read/write clauses (one read + one write per
world), one body. Inside the body:

- `sim-Pos-read-cap : XReadCap<SimWorld, Pos>` is in scope (from
  `:reads-from sim [Pos]`);
- `ren-RenderPos-write-cap : XWriteCap<RenderWorld, RenderPos>` is in
  scope (from `:writes-to ren [RenderPos]`) and is auto-consumed at body
  end.

## Capabilities are keyed on *(World, Component)*

`ecs/cap` (single-world) keys a capability on the component alone:
`WriteCap<T>`. `ecs/xcap` keys it on the pair: **`XWriteCap<W, T>`** and
**`XReadCap<W, T>`**. The world is part of the type, so
`XWriteCap<RenderWorld, Pos>` and `XWriteCap<SimWorld, Pos>` are
nominally distinct — the same component, different lock targets.

This is what makes the static guarantee real. A `set-<Comp>!` accessor
minted (by `sized-defcomponent-accessors-xmono`) for `RenderWorld` only
accepts a `RenderWorld`-keyed cap:

```turmeric
;; XWriteCap<SimWorld, Pos> passed to a RenderWorld setter:
(set-Pos! sim-keyed-cap ren 0 (:: 1 :Pos))
;; error [TUR-E0001]: expected XWriteCap<RenderWorld, Pos>,
;;                    got XWriteCap<SimWorld, Pos>
```

So a body cannot write into a world it only declared `:reads-from`: the
only cap in scope for that world is a read cap, and a write cap minted
elsewhere has the wrong world type. Writing a component a clause did not
name is likewise a compile error — the `<w>-<C>-write-cap` binding
simply does not exist.

`XWriteCap` is `^linear` (consumed exactly once, enforced under
`-Xsubstructural`); `XReadCap` is non-linear and re-borrowable across a
loop.

## Static non-conflict, lifted over worlds

`ecs/xstage` schedules `XSystem`s. Each is bound to concrete worlds with
`bind-xsystem`, which supplies a **world-id** (the conflict key) and a
boxed handle per slot:

```turmeric
(let [xs (xstage-new)
      ws (bind-xsystem extract-renderables 0 sim-ptr 1 ren-ptr)]
  (xstage-add! xs ws)
  (xstage-run! xs))      ;; waves run sequentially; each wave in parallel
```

Two systems conflict iff, **for some world they both touch** (equal
world-id on a pair of slots), the v1 mask rule fires (a write overlaps
the other's reads-or-writes). Worlds touched by only one of them are
independent lock targets. So:

- write `(sim, Pos)` and write `(ren, Pos)` — same component, different
  worlds — **never conflict** and coalesce into one parallel wave;
- write `(ren, Pos)` from two systems **conflict** and split into two
  waves.

This is the v1 rule applied point-wise over the set of worlds, with the
world-id as the per-target discriminator. (Single-world cross-world
scheduling — one world per system — already shipped as `ecs/hstage`;
`ecs/xstage` is the two-or-more-worlds-per-system generalisation — see
[Three or more worlds](#three-or-more-worlds).)

### Cycles are rejected

If S writes a `(world, component)` that T reads **and** T writes a
`(world, component)` that S reads, no wave order satisfies both — the
stage is ill-formed. `xstage-has-cycle?` is the static check:

```turmeric
(when (xstage-has-cycle? xs)
  (panic "cross-world stage has a cyclic dependency; split with a barrier"))
```

Split a cyclic pair into two sequenced stages instead.

## Three or more worlds

`defxsystem` is not limited to two worlds. List as many world bindings as
the pipeline needs; the per-`(world, component)` lock model is identical,
only the bookkeeping arity grows. The motivating shape is a
snapshot → predicted → render pipeline: read the authoritative state,
write a predicted view and a render view in one pass.

```turmeric
(import ecs/xsystem :refer [defxsystem])
(import ecs/xstage  :refer [bind-xsystem-n xstage-add-n!
                            xstage-new xstage-run!])

(defxsystem project
  [auth AuthWorld  pred PredWorld  ren RenderWorld]
  :reads-from auth [Pos]
  :writes-to  pred [PredPos]
  :writes-to  ren  [RenderPos]            ;; clauses in any order;
  ;; auth has no writes clause and pred / ren have no reads clause —
  ;; the empty sides are simply omitted (CLAUSE-V0).
  (do
    (set-PredPos!   pred-PredPos-write-cap   pred 0
      (:: (:: (get-Pos auth-Pos-read-cap auth 0) :int) :PredPos))
    (set-RenderPos! ren-RenderPos-write-cap  ren  0
      (:: (:: (get-Pos auth-Pos-read-cap auth 0) :int) :RenderPos))))
```

A three-or-more-world system is bound with **`bind-xsystem-n`** — one
`world-id handle` pair per slot, in declaration order — and added with
**`xstage-add-n!`**:

```turmeric
(let [xs (xstage-new)
      bs (bind-xsystem-n project  0 auth-ptr  1 pred-ptr  2 render-ptr)]
  (xstage-add-n! xs bs)
  (xstage-run! xs))
```

The same conflict, wave, and cycle logic applies across every slot: two
`project` bindings that share the `pred` world both write `(pred,
PredPos)`, so they serialise into two waves; bound to disjoint worlds
they coalesce into one. **Every declared world must appear in at least
one clause** — a world listed in the binding vector but never read or
written is a "did you mean to use it?" smell and is rejected at compile
time, naming the dropped world.

The two-world surface (`make-xsystem` / `bind-xsystem` / `xstage-add!`)
is unchanged and remains the right tool when there are exactly two
worlds; `bind-xsystem-n` / `xstage-add-n!` are the N-world generalisation
(`N >= 3`).

## Mirror sugar

`ecs/xmirror`'s `defmirror` writes the one-component verbatim copy in
one line:

```turmeric
(import ecs/xmirror :refer [defmirror])

(defmirror mirror-pos
  [sim SimWorld  ren RenderWorld]
  :count 1024
  :from  Pos
  :to    RenderPos)
```

It lowers to exactly the `extract-renderables` `defxsystem` above, looped
over `0..count-1`. For multi-component mirrors or a non-trivial
projection, write the `defxsystem` directly — `defmirror` deliberately
covers only the common case "without losing the explicit form for harder
transforms."

## Entity identity does not cross worlds

An entity id in `sim` is *not* an entity id in `ren`. They are different
worlds with independent slot allocation. To correlate entities across
worlds, store the other-world id in a component
(`RenderLink { sim-id : int }`), the same discipline Bevy enforces with
`MainEntity` markers. Cross-world query filters (`with`/`without` across
the boundary) are not provided — join in user code.

## Comparison to precedents

|                          | Bevy SubApp                          | Unity DOTS                              | tur-ecs cross-world                              |
|--------------------------|--------------------------------------|-----------------------------------------|--------------------------------------------------|
| Worlds in one system     | No — subapps run in sequence (Extract)| No — per-World; `EntityCommandBuffer`   | **Yes** — one system, two worlds, declared r/w   |
| Non-conflict guarantee   | Per-world dynamic; subapps serialised | Per-world dynamic; ECB at sync points   | **Static, per (world, component)**               |
| Entity identity          | `MainEntity` holds source-world id    | Explicit translation tables             | `RenderLink { sim-id }` user pattern             |
| Cycles                   | Allowed (subapps ordered)             | Allowed (sync points serialise)         | **Rejected** (`xstage-has-cycle?`)               |

Bevy gets parallelism by running the SubApp later in the frame; tur-ecs
gets it by *proving* the systems cannot conflict.

## Notes on the surface vs the plan

Two cosmetic deviations from the plan's illustrative syntax, both forced
by how `tur` tokenises and resolves names today:

- **Caps are `XWriteCap`/`XReadCap`, not an overloaded `WriteCap`.** `tur`
  resolves an opaque type constructor by name in a single global
  namespace, so a two-param `WriteCap [W T]` collides (kind mismatch)
  with `ecs/cap`'s one-param `WriteCap [T]` whenever both are linked —
  and every cross-world program links `ecs/cap` transitively through
  `ecs/sized-world`. Unifying them behind one name (the plan's
  "single-world collapses to `WriteCap<DefaultWorld, T>`") is a follow-up
  gated on per-module opaque namespacing or type aliases.
- **World bindings use a bare type symbol** — `[sim SimWorld ren
  RenderWorld]`, not `[sim :SimWorld …]`. `tur` tokenises an attached
  `:SimWorld` as a keyword, and its real type ascriptions use a
  standalone `:`.

`load-<WorldType>` (int-carrier → typed world) must be in scope for each
world the system drives, the same convention `sized-defsystem-scheduled`
uses. As of GEN-V0 the trio comes from a single macro call:

```turmeric
(import ecs/xworld :refer [defworld-box-helpers])

(sized-defworld-mono SimWorld (Static 8) [Pos])
(defworld-box-helpers SimWorld)
;; emits box-SimWorld / load-SimWorld / free-SimWorld-box,
;; each a thin wrapper over the polymorphic box-world / load-world /
;; free-world-box helpers in `ecs/xworld`.
```

Pre-GEN-V0 fixtures hand-rolled the three inline-C blocks per world
(see `tests/xworld-extract.tur`); new cross-world setups should reach
for `defworld-box-helpers` instead.

## Tests

- `spices/ecs/tests/xworld-extract.tur` — two-world spawn / extract / verify.
- `spices/ecs/tests/xworld-scheduling.tur` — cross-world non-conflict
  coalescing vs same-(world,component) serialisation (wave counts).
- `spices/ecs/tests/xworld-cycle.tur` — cyclic stage detected, acyclic not.
- `spices/ecs/tests/xworld-mirror.tur` — `defmirror` matches the
  hand-written extract.
- `spices/ecs/tests/xworld-3world.tur` — three-world snapshot → predicted
  → render extract (`bind-xsystem-n` / `xstage-add-n!`); one wave for a
  lone system, two for conflicting bindings (N-W-V0).
- `spices/ecs/tests/xstage-wide-wave.tur` — 200 systems in one wave all
  run, with no 64-system truncation (CAP-V0).
- `spices/ecs/tests/xsystem-clause-flex.tur` — clauses in any order, empty
  sides omitted (CLAUSE-V0).
- `spices/ecs/tests/xworld-defbox.tur` — `defworld-box-helpers` emits the
  per-world `box-<W>` / `load-<W>` / `free-<W>-box` trio; round-trips
  identical values to the hand-written-helpers fixture (GEN-V0).
- `spices/ecs/tests/errors/xworld-wrong-world-cap.tur` — wrong-world cap
  rejected (TUR-E0001).
- `spices/ecs/tests/errors/xworld-undeclared-write.tur` — undeclared
  write rejected (TUR-E0003).
- `spices/ecs/tests/errors/xworld-unused-world.tur` — a declared world
  with no clause rejected (TUR-E0003) (N-W-V0).
