# tur-ecs

Entity-Component-System for Turmeric. Pairs with `tur-raylib` for
real-time games. Long-form plan and rationale:
[`docs/archive/ecs-spice-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/ecs-spice-plan.md).

## Status

**E1 -- variadic-looking queries + row-typed `Query` value.** Shipped 2026-06-11.

Since then (unreleased): **E2** systems + parallel scheduler with
substructural write-cap enforcement; **E2d** associated-type storage
projection (the world's field types project through `(Storage Comp)`)
plus the `StorageOps` backend class; and **E2c** bounded-capacity
("sized") worlds -- see the dedicated section below. See `CHANGELOG.md`
for the per-slice log.

The D1 prerequisite (variadic HKT rows) and all four L6 follow-ups
(strict row elements, `^&` on defgadt/ADT/deftype, row-polymorphic
defn/fn, permutation-aware row equality) landed in the main repo this
session, which unblocked the E1 surface. See
`../../../turmeric/docs/archive/history/variadic-hkt-rows-missing.md`.

### What's in

- `ecs/entity` -- generationally-versioned opaque `Entity` (low 32 = index,
  high 32 = generation), construct/pack/unpack helpers.
- `ecs/storage` -- dense storage: `(dense-new) -> int`, `dense-set!`,
  `dense-get`, `dense-has?`, `dense-len`, `dense-free`. Per-instantiation
  monomorphization sizes the data array correctly for both int-carried
  opaques and by-value structs; annotate the let binding
  (`(let [p : Pos (dense-get s i)] ...)`) to drive the generic into a
  struct-specialised clone.
- `ecs/sparse` -- open-addressed Robin Hood hash storage:
  `sparse-new/len/set!/get/has?/del!/free`. Backward-shift deletion
  driven by `probe_dist`; rehash at load factor 0.75. Use when "few
  entities carry this component" -- ~16 bytes per *populated* slot vs
  dense's one element-sized slot per *world entity*.
- `ecs/tag` -- bitset storage for zero-payload markers (`Dead`,
  `Frozen`, `Player`). `tag-new/cap/count/set!/clear!/has?/free`. O(1)
  set/test, popcount via `__builtin_popcountll`.
- `ecs/world` -- `defworld Name [Comp1 ... CompN]` macro (variadic since
  E2d-P5b -- no arity cap; a recursive `world-fields` helper computes the
  flat field list at expansion time. Mixed dense/sparse/tag fields all
  hold an `:int` handle and the same `(.Comp w)` syntax works regardless
  of backend). `world-alloc-entity!`, `world-despawn!`.
- `ecs/query` -- single user-facing **`for-each`** macro, truly
  variadic (no arity cap; recursive helper macros walk the component
  list at expansion time):
  ```
  (for-each w [Pos Vel]      [e p v]      body)   ;; 2 components
  (for-each w [Pos Vel Hp]   [e p v h]    body)   ;; 3 components
  (for-each w [A B C D]      [e a b c d]  body)   ;; 4 components
  (for-each w [A B ... J K]  [e a ... j k] body)  ;; 11 components, no cap
  ```
  Thin `for-each1`..`for-each3` shims stay exported for back-compat.
  `world-tagged?` / `world-untagged?` consult a tag-bitset
  field on the world. Compose tag filters with `when` / `unless` inside
  the body to express `with` / `without` constraints.
- `ecs/query` ships **`defquery`** + **`run-query!`** -- the functional
  packaging from the plan:
  ```
  (defquery integrate w GameWorld [Pos Vel] [e p v]
    (dense-set! (.Pos w) e (+ p v)))

  (run-query! integrate world)
  ```
  `defquery` desugars to a `(defn integrate [^borrow w : GameWorld] : nil ...)`
  whose body is the `for-each` iteration; `run-query!` is sugar for
  invoking it. The world is taken as `^borrow` so callers can reuse the
  world value after running the query.
- `ecs/query` also exposes the row-typed **`Query`** value:
  ```
  (defstruct Query [^&in ^&out] (world :int))
  ```
  Row arguments are *phantom* -- the variadic-HKT-rows work erases them
  at codegen, so a `Query` carries only the world handle at runtime,
  but two `Query`s with different `(in, out)` row arguments are
  distinguished at the type level. Typical use:
  ```
  ;; A Query against GameWorld that reads Pos+Vel and writes Pos:
  (defn integrate [q : (Query #row{Pos Vel} #row{Pos})] : nil ...)
  ```
  Requires `-Xdata-literals` at the consumer's build for the
  `#row{...}` literal syntax. The spice module itself does not use
  `#row{...}`, so it builds under the default reader.

### Bounded-capacity (sized) worlds -- E2c

A parallel surface where the world commits to a capacity `n` at
construction and threads it through every storage as a type-level
index, so iteration is *statically rectangular* -- the
`sized-for-each` loop bound comes from the world's type, not a runtime
min-capacity probe. Design plan:
[`docs/archive/ecs-sized-world-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/ecs-sized-world-plan.md).

- `ecs/sized-storage` / `ecs/sized-sparse` / `ecs/sized-tag` -- the
  sized counterparts of the three backends, each a phantom-indexed
  opaque (`(SizedDense n A)`, `(SizedSparse n A)`, `(SizedTag n)`) so
  the same `n` unifies across a mixed-representation world.
- `ecs/sized-world` -- `(sized-defworld GameWorld [Comp ...])` lowers
  to a polymorphic `(GameWorld n)` struct (one `(SizedDense n Comp)`
  field per component plus a state cell) and a `make-GameWorld`
  constructor; `(sized-defworld-mono GameWorld (Static 64) [...])`
  bakes the capacity in at declaration for application code with a
  fixed budget. `sized-defcomponent-accessors` emits the cap-gated
  `get-/set-!/has-?` family; `sized-defsystem` is the sized
  counterpart of `defsystem` (same `:reads`/`:writes` cap-gating).
- **spawn / despawn.** Two spawn variants per the design plan's Q3:
  `sized-spawn! : ... -> int` aborts on a full world (benchmark / demo
  paths), while `sized-spawn : ... -> (Result int WorldFull)` reports
  capacity exhaustion as a typed `(err world-full)` rather than
  aborting. Both share the slot-allocation path (free-list reuse, then
  `next` high-water advance) and return a generationally-packed entity.
  `sized-despawn` frees a slot and bumps its generation; `sized-alive?`
  answers the use-after-despawn question (a stale handle whose
  generation no longer matches the slot's reads as dead).
- `sized-defworld-copy-into` emits a `copy-into-<World>` that
  slot-copies a world into a larger one, preserving entity handles
  across the resize (the generation array is carried over).
- `sized-defworld-world-resize` emits a `world-resize-<World>` that
  grows a world to a fresh *runtime* capacity and hands it back inside
  the size-hiding existential `(exists [n'] (<World> n'))`. It is the
  thin client layer over `copy-into-<World>`: allocate the larger
  destination, copy (handles preserved), and `pack` the result so
  callers `open` it to recover an abstract `n'` and run
  `sized-for-each` / the cap-gated accessors against the resized world.
  The result is existential because the new capacity is a runtime
  value, not a static `(Static k)`. (Unblocked by turmeric's
  existential pack/open heap-boxing fix for multi-field struct
  payloads.)

### What's not in (yet)

- Sparse/tag participation in `for-each` iteration order. Today the
  primary iteration walks the dense-storage union; a sparse-primary
  variant is straightforward but unwritten.
- Systems and the parallel scheduler -- shipped in E2 prior to this.
- raylib companion + demo -- ships in E3.

## Smoke / regression tests

```sh
tur run tests/spawn1k.tur                 # E0 int-carrier dense (sum = 499500)
tur run tests/spawn1k-pos.tur             # E1' multi-field Pos via dense-get (sum = 499500)
tur run tests/sparse-rt.tur               # E1' sparse insert / get / has / del
tur run tests/sparse-rt-large.tur         # E1' patch-1 RH regression (500/250/0)
tur run tests/sparse-stress.tur           # E1' patch-1 10k mixed ops vs bitset (-> ok)
tur run tests/tag-rt.tur                  # E1' tag set / has / clear / popcount
tur run tests/integrate2.tur              # E1' for-each2 Pos+Vel integrate (sum = 125750)
tur run tests/filter-with-without.tur     # E1' tag filters inside body (sum = 1368)
tur run tests/for-each-variadic.tur       # E1  variadic (for-each w [Pos Vel] ...) (125750)
tur run tests/for-each-arity-4.tur        # E1  arity-4 dispatch (49500)
tur run tests/for-each-arity-5.tur        # E1  arity-5 dispatch (18375)
tur run tests/for-each-arity-8.tur        # E1  arity-8 (15660)
tur run tests/for-each-arity-12.tur       # E1  arity-12 no-cap demo (3510)
tur run tests/defquery-integrate.tur      # E1  defquery + run-query! (125750)
tur run -Xdata-literals tests/query-typed.tur   # E1  row-typed Query value (prints 42)
tur run tests/sized-world-spawn.tur       # E2c sized-spawn! / despawn / live / cap
tur run tests/sized-world-spawn-result.tur # E2c fallible sized-spawn -> (Result int WorldFull)
tur run tests/sized-for-each.tur          # E2c statically-rectangular sized-for-each
tur run tests/sized-world-resize.tur      # E2c world-resize -> (exists [n'] (World n')) grow
```

Each exits 0 on success.

## Known limitations

Each of the original prerequisite gaps (A-I, archived at
[`docs/archive/history/ecs-prereq-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/history/ecs-prereq-plan.md))
has shipped; the residual list below is empirical limits of the
v1 surface, not language gaps.

1. **`for-eachN` iterates dense storages only.** Listing a sparse or
   tag component on the primary loop would either need a different
   primary (smallest-set iteration) or a dense-then-test fallback.
   Today, use sparse/tag components only via filters inside the body.

2. **`defsystem` bodies cannot reach a typed world -- use
   `defsystem-world`.** `defsystem` is never told which world it runs
   against, so it binds `w : int` and the body must reinterpret that
   carrier itself. Since worlds became by-value aggregates, the old
   `(:: w GameWorld)` bridge is `TUR-E0295`, so the typed
   `get-<Comp>` / `set-<Comp>!` accessors are unreachable from a plain
   `defsystem`. Use `defsystem-world` (which takes the world type and
   lowers to a typed impl plus an int-carrier wrapper, exactly as
   `sized-defsystem-scheduled` does) when the body needs its world;
   keep `defsystem` when the int carrier is all you want.

3. **`ecs/xworld` shadows `ecs/cap`.** `ecs/xcap` exports `use-cap!`,
   `make-write-cap` and `make-read-cap` under the same names as
   `ecs/cap`, and `ecs/xworld` pulls them in. A file importing both
   must import `ecs/xworld` **before** `ecs/cap`, or the two-parameter
   `XWriteCap` versions win and every component-only cap call fails with
   `TUR-E0001`. This bites `defsystem-world` users in particular, since
   the `load-<World>` its expansion names comes from `ecs/xworld`.

### Performance

`bench/` measures the surface against hand-rolled C: 100k entities,
dense float `Pos`/`Vel`, 100 frames. `for-each2` runs **8.42x**
hand-rolled and `sized-for-each` **3.50x** -- the archived plan's
within-2x target is not met. The gap is not Turmeric's codegen (a raw
Turmeric loop over flat buffers runs 1.04x C) and not the query macro;
it is the unsized `dense-set!` write path, which carries an auto-grow
capacity branch, a `present[]` byte write and a `len` update per store.
Prefer sized worlds where the entity budget is known. See
[`bench/README.md`](https://github.com/turmeric-lang/turmeric-spices/blob/main/spices/ecs/bench/README.md).

### Breaking change in the I3-I4 ship (2026-06-11)

`defsystem`'s `:reads`/`:writes` are component-name vectors, not
bitmask ints. Per-component CIDs use the `<Comp>-cid` titlecase
convention (`Pos-cid`, not `pos-cid`). Required imports at the call
site:

```turmeric
(import ecs/system :refer [defsystem])
(import ecs/cap    :refer [WriteCap ReadCap make-write-cap make-read-cap use-cap!])
```

Then:

```turmeric
;; Before (E2 surface):
(defsystem physics
  (cid-bit pos-cid)
  (cid-bit pos-cid)
  body)

;; After (I3 surface):
(defsystem physics
  [Pos]
  [Pos]
  body)
```

`defcomponent-accessors` now emits `set-<Comp>!` / `get-<Comp>` that
require a `WriteCap<Comp>` / `ReadCap<Comp>` first arg; pass either
the `<Comp>-write-cap` / `<Comp>-read-cap` bound by `defsystem` in
body scope, or a freshly minted cap from `make-write-cap` /
`make-read-cap` (gated by the call site's declared `:writes`). A body
that writes a component it did not declare now fails to elaborate.
See the original report at
[`docs/archive/history/ecs-defsystem-write-caps-not-enforced.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/history/ecs-defsystem-write-caps-not-enforced.md).

## See also

- [ECS guide](https://turmeric-lang.com/docs/html/guides/ecs-guide.html)
- [ECS storage guide](https://turmeric-lang.com/docs/html/guides/ecs-storage-guide.html)
- [ECS cross-world guide](https://spices.turmeric-lang.com/docs/html/guides/ecs-cross-world-guide.html)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/ecs>

## License

MIT.
