# `tur-crdt` -- conflict-free replicated data types

Two replicas edit the same data while disconnected. When they reconnect, they
have to agree -- without a coordinator, without a lock, and without either
side's work being silently thrown away. A CRDT is a data type whose merge is
associative, commutative and idempotent, which is exactly the set of
properties that makes "agree" fall out of arithmetic instead of out of a
protocol.

Turmeric had no vocabulary for this at all: greps for `Semigroup`, `Monoid`,
`semilattice`, `CRDT` and `LWW` across `stdlib/`, `src/` and every spice
returned nothing. The classes now live in `stdlib/typeclass-lattice.tur`
(`JoinSemilattice`, `BoundedJoin`, and the law helpers); this spice is the
data types built on them.

What Turmeric brings that a typical CRDT library has to build first: a
persistent HAMT for the dot stores, real typeclasses so `(ORMap V)` can be a
lattice exactly when `V` is, and associated types so a delta can be a
different type from the state it came from -- which, it turns out, it has to
be for the causal types.

**Zero inline C**, in every module. That is deliberate rather than incidental:
a single inline-C block would make the code unrunnable under `tur --interpret`
(the compiler repo's interpreter harness PASS-skips any program containing
one), and a CRDT's merge is precisely the thing worth exercising on both back
ends. It is the opposite trade from a spice like `secret`, where the
guarantees ARE the inline C.

## Status

| Phase | Contents | State |
|-------|----------|-------|
| C1 | `crdt/lattice`, `crdt/counter` -- `GCounter`, `PNCounter` | shipped |
| C2 | `crdt/causal`, `crdt/set` -- `GSet`, `TwoPSet` -- and `crdt/orset` | shipped |
| C3 | `crdt/hlc`, `crdt/register`, `crdt/ormap` | shipped |
| C4 | `DeltaCRDT` and delta mutators for every type above | shipped |
| C5 | `crdt/rga` -- the replicated sequence | shipped |
| C6 | `crdt-sync` -- transport, delta buffering, anti-entropy | not started, separate spice |

Design plan:
[`docs/upcoming/crdt-spice-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/crdt-spice-plan.md)
in the compiler repo. Eleven test suites, including two seeded convergence
fuzzers.

## The one idea

Every type here is a `JoinSemilattice`: `join` combines two states, and it does
not care what order you call it in, how you group the calls, or how many times
the same state arrives.

```turmeric no-check
(join a b)          ;; = (join b a)                  -- commutative
(join (join a b) c) ;; = (join a (join b c))         -- associative
(join a a)          ;; = a                           -- idempotent
```

Those three together are why a CRDT survives a network that reorders,
duplicates and delays. You do not need an exactly-once link; you need a merge
that does not mind.

`join-all` folds a `Vec` of states from `bottom`, and `bottom` is the empty
state of the type.

## Quick start

Two replicas count independently and converge on merge:

```turmeric
(defmodule demo
  (import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value])

(defn alice [] : GCounter
  (gcounter-bump (gcounter-new) (replica (quote alice)) 3))

(defn bob [] : GCounter
  (gcounter-bump (gcounter-new) (replica (quote bob)) 5))

(defn main [] : int
  (println (gcounter-value (join (alice) (bob))))   ;; 8, in either order
  0)
)
```

```sweet-exp
defmodule demo
  (import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value])
  defn alice [] : GCounter
    gcounter-bump(gcounter-new() replica((quote alice)) 3)
  defn bob [] : GCounter
    gcounter-bump(gcounter-new() replica((quote bob)) 5)
  defn main [] : int
    println $ gcounter-value $ join (alice) (bob)   ;; 8, in either order
    0
```

Every `turmeric` example in this file is a whole program, and each was
extracted and run against this version of the spice -- the values in the
trailing comments are what it printed. `import` is only legal inside a
`defmodule`, which is why the wrapper is there.

You do not need to `(load "stdlib/typeclass-lattice.tur")` yourself -- each
module loads it, and the classes are ambient once any of them is in the build.

## The catalog

| Type | Module | State | Join | Reading |
|------|--------|-------|------|---------|
| `GCounter` | `crdt/counter` | replica -> its own total | pointwise max | sum |
| `PNCounter` | `crdt/counter` | two `GCounter`s | per side | P - N |
| `GSet` | `crdt/set` | a set | union | membership |
| `TwoPSet` | `crdt/set` | adds + tombstones | per side | added and not removed |
| `ORSet` | `crdt/orset` | elem -> dots, + context | add-wins, dot-aware | has a surviving dot |
| `LwwRegister` | `crdt/register` | value + `Hlc` | later stamp wins | the value |
| `MvRegister` | `crdt/register` | set of values + `Hlc` | union | every concurrent value |
| `ORMap V` | `crdt/ormap` | keys by dot, values by `V`'s join | add-wins + recursive | the value at a key |
| `Rga` | `crdt/rga` | ordered nodes + tombstones | node union, re-integrated | the visible sequence |

Elements and keys are `Sym`; register and map values are `int` carriers. That
is a real restriction, not a stylistic one -- see **Limits** below.

## Counters -- `crdt/counter`

A `GCounter` is a map from replica to that replica's own total, joined by
pointwise maximum. Only the replica itself may bump its slot, which is what
makes the max a correct merge: a disagreement about a slot is always "one of
us has seen fewer updates", never a conflict.

```turmeric no-check
(gcounter-new)                      ;; : GCounter
(gcounter-bump c r n)               ;; add n to r's own total
(gcounter-value c)                  ;; the sum -- the query, not the lattice
(gcounter-replicas c)               ;; how many entries -- the state's SIZE
```

A `PNCounter` is two `GCounter`s, one for increments and one for decrements,
read as `P - N`. That is the standard trick for a counter that can go down
without either half ever decreasing.

`replica` names a replica. `ReplicaId` is a transparent `defalias` for `Sym`
and cannot appear in an export list, so callers spell `Sym` or use `replica`.

## Sets -- `crdt/set` and `crdt/orset`

Three designs, and the differences are the whole point:

- **`GSet`** only grows. Union is associative, commutative and idempotent for
  free, so this is the baseline every other set is measured against.
- **`TwoPSet`** buys removal with a second grow-only set of tombstones. The
  trade is permanent: once removed, an element can never be re-added.
- **`ORSet`** buys re-adding back, and pays with a per-element dot store. Add
  wins over a CONCURRENT remove; a remove that causally follows an add still
  removes it.

```turmeric
(defmodule demo
  (import crdt/orset :refer [ORSet orset-new orset-add orset-remove orset-has?
                             orset-count])

(defn main [] : int
  (let [s (orset-remove
            (orset-add (orset-add (orset-new) (quote apple) (quote alice))
                       (quote pear) (quote alice))
            (quote apple))]
    (println (orset-count s)))     ;; 1 -- pear
  0)
)
```

A removal forgets the dots this replica has OBSERVED rather than writing a
tombstone. A concurrent add elsewhere carries a dot this replica never saw, so
it survives the merge. That is add-wins, and it is the reason `crdt/causal`
exists.

## Causality -- `crdt/causal`

A `Dot` is `(replica, counter)`: one replica's n-th event. Nothing else ever
mints that pair, so a dot names an event uniquely with no coordination.

Two containers of dots, and the difference between them is load-bearing:

- **`DotContext`** -- the dots a replica has OBSERVED, compacted to a version
  vector (replica -> highest counter). `alice -> 3` means alice's dots 1, 2
  and 3. Exact while each replica's dots arrive without gaps, which is what
  causal delivery gives you.
- **`DotSet`** -- an EXACT set of dots. `{alice@3}` means that dot and no
  other.

A state wants the first; a **delta wants the second**, and getting that wrong
is silent. See **Deltas** below.

## Clocks -- `crdt/hlc`

An LWW register needs a total order on writes, and wall time is not one: it
runs backwards under NTP, and two replicas disagree by more than the latency
that would have ordered them causally.

An `Hlc` keeps a wall component and a logical counter, with the replica name
as a final tiebreak so the order is TOTAL -- necessary, since two writes must
never be "equal but different". `hlc-max-drift` bounds how far ahead of local
time a remote stamp may drag this clock; a stamp beyond it is not adopted.

**Every operation takes `now` as a parameter** rather than calling a clock.
That makes the tests exact without a mock, and keeps the spice inline-C free.

## Registers -- `crdt/register`

`LwwRegister` picks a winner by HLC stamp. Every replica picks the same one,
which is what makes the join a join. **The cost is that a concurrent write is
silently discarded** -- right when the value is a fact one writer is
authoritative for, wrong when it is an edit someone will look for again.

`MvRegister` keeps every concurrent value and makes the caller resolve them.
The cost is that a read is a set.

```turmeric
(defmodule demo
  (import crdt/register :refer [LwwRegister lww-new lww-set lww-value
                                MvRegister mv-new mv-set mv-values])

(defn main [] : int
  ;; Later stamp wins.
  (println (lww-value (join (lww-set (lww-new (quote alice)) 1 1000)
                            (lww-set (lww-new (quote bob)) 2 2000))))   ;; 2
  ;; Concurrent writes: both survive.
  (println (mv-values (join (mv-set (mv-new (quote alice)) 1 1000)
                            (mv-set (mv-new (quote bob)) 2 1000))))     ;; 2
  0)
)
```

## Maps -- `crdt/ormap`

The type that makes the rest compose. Its merge has two jobs at once: decide
which KEYS survive (the OR-Set problem, via dots and a context) and merge the
VALUES of keys both sides hold.

`(ORMap V)` is a `JoinSemilattice` exactly when `V` is, so `join` on the map
dispatches to `V`'s own instance:

```turmeric
(defmodule demo
  (import crdt/ormap :refer [ORMap ormap-new ormap-put ormap-get ormap-count])

(defn hi [] : (ORMap MaxI)
  (:: (ormap-put (:: (ormap-new) (ORMap MaxI))
                 (quote alice) (quote temp) (:: 21 MaxI))
      (ORMap MaxI)))

(defn lo [] : (ORMap MaxI)
  (:: (ormap-put (:: (ormap-new) (ORMap MaxI))
                 (quote bob) (quote temp) (:: 19 MaxI))
      (ORMap MaxI)))

(defn main [] : int
  (println (:: (:: (ormap-get (:: (join (hi) (lo)) (ORMap MaxI)) (quote temp))
                   MaxI)
               int))                                   ;; 21 -- MaxI's join
  0)
)
```

`ormap-merge-with` passes the value merge explicitly instead, which is
strictly more general: two maps over one value type can merge differently.

**Every `ORMap`-valued binding needs an ascription.** `V` is phantom -- carried
by no field -- so there is nothing to infer it from, and an un-ascribed
binding is declared at the int64 carrier while the constructor returns a
pointer. `tur check` passes and `cc` rejects it, and only on clang >= 21,
where `-Wint-conversion` is an error. It is a silent macOS-only build failure
if you forget.

## Deltas -- C4

Shipping the full state on every change is what makes naive state-based CRDTs
unusable over a link. A delta mutator returns a small value the receiver folds
in with `apply-delta`:

```turmeric
(defmodule demo
  (import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value gcounter-delta-bump])

(defn alice [] : GCounter
  (gcounter-bump (gcounter-new) (replica (quote alice)) 3))

(defn bob [] : GCounter
  (gcounter-bump (gcounter-new) (replica (quote bob)) 5))

(defn main [] : int
  ;; alice bumps again and ships only the delta; bob folds it in.
  (let [d (gcounter-delta-bump (alice) (replica (quote alice)) 2)]
    (println (gcounter-value (apply-delta (bob) d))))   ;; 10
  0)
)
```

The guarantee, asserted per type in `tests/crdt/test_delta.tur`: **applying a
sequence of deltas equals joining the full states.** Deltas also `join` with
each other, so a sender can buffer several and ship one.

Mutators: `gcounter-delta-bump`, `pncounter-delta-inc` / `-dec`,
`gset-delta-add`, `twopset-delta-add` / `-remove`, `orset-delta-add` /
`-remove`, `lww-delta-set`, `mv-delta-set`, `ormap-delta-put` / `-remove`,
`rga-delta-insert` / `-delete`.

### The delta TYPE differs, and only for the causal types

| | `Delta` |
|---|---|
| `GCounter`, `PNCounter`, `GSet`, `TwoPSet`, `LwwRegister`, `MvRegister`, `Rga` | the state type |
| **`ORSet`, `ORMap`** | **`OrsetDelta` / `OrmapDelta`** |

A state's `DotContext` is a compacted version vector, so a delta carrying one
would claim every dot BELOW the ones it names. Measured: an add-delta from
alice@3 carrying `{alice: 3}` makes the receiver read its own untouched
elements as deliberately removed. Delivering one delta to a replica holding
two elements left it holding one, with no error. So the causal deltas carry an
exact `DotSet` of what they retract, and they are a different type.

### Deltas require causal delivery

Applying a delta folds its exact dots into the receiver's version vector, and
that is lossless only while a replica's dots arrive without gaps. That is the
same assumption the version-vector compaction already makes; C6's sync layer
is where it gets paid for.

## Sequences -- `crdt/rga`

Every other type here merges values that have no position. A sequence cannot:
two replicas insert at "index 3" of a document they both believe they are
looking at, and an index is meaningless by the time the other side sees it.

So an RGA never ships an index. Each element carries an id and the id of the
element it was inserted AFTER, and the document order is a function of that
forest.

```turmeric
(defmodule demo
  (import crdt/rga :refer [Rga rga-new rga-append rga-insert rga-delete
                           rga-get rga-len rga-size])

(defn main [] : int
  (let [d (rga-append (rga-append (rga-new) (quote alice) 72) (quote alice) 105)]
    (println (rga-len d))      ;; 2
    (println (rga-get d 1)))   ;; 105 -- character codes
  0)
)
```

Three consequences worth knowing before you use it:

1. **Ids are Lamport**, not per-replica, so a node's id always exceeds its
   origin's. The integration scan is correct only under that invariant.
2. **Concurrent runs do not interleave.** `abc` and `xyz` typed concurrently
   merge to one run then the other, never `axbycz`. That is the property a
   convergence test cannot check, and it has its own scenario in
   `tests/crdt/test_rga.tur`.
3. **Deletes are tombstones**, because a later insert may name a deleted
   element as its origin. `rga-size` therefore diverges from `rga-len` under
   churn.

Complexity, stated rather than implied: insert and delete are O(n), merge is
O(n*m). A production sequence CRDT indexes nodes by id and keeps a balanced
index for the position lookup. This one is written to be read.

## Working with affine states

**This is the gotcha that will bite first.** Every state here holds a Map or
Vec handle, so it is affine: `join` and every mutator CONSUME their argument.
Two assertions over the same state need two states.

```turmeric no-check
;; WRONG -- `a` was moved into the first join
(let [m1 (join a b)
      m2 (join a c)]   ;; TUR-E0005: use after move
  ...)

;; Right -- rebuild, and let the builder be a function
(defn a [] : GCounter ...)
(let [m1 (join (a) (b))
      m2 (join (a) (c))]
  ...)
```

Readers take `^borrow` and do not consume. This reads differently from the
same code in a GC'd language, and it is more honest than it first looks: a
replica does not hand its state away and keep using it either.

## When not to use this

- **`Rga` grows without bound under churn**, and it is the only type here
  that does. Measured, 50 add-then-delete cycles on one element:

  | type | live | retained |
  |------|------|----------|
  | `ORSet` | 0 | 0 dots, context width **1** |
  | `TwoPSet` | 0 | **2** (one add, one tombstone) |
  | `Rga` | 0 | **50** nodes |

  The three differ because of what they retain. `ORSet` and `ORMap` keep only
  a `DotContext`, which is a version vector -- **one entry per replica**, not
  per operation -- so churn does not grow them. `TwoPSet`'s two halves are
  SETS, so repeating an element is free; it grows with the number of
  DISTINCT elements ever touched, which is unbounded only if your key space
  is. `Rga` keeps one node per insert forever, because a later insert may
  name a deleted element as its origin -- so a document with 1000 characters
  typed and 1000 deleted reads `rga-len` 0 and `rga-size` 1000, and it is the
  second number that crosses the wire.

  Compaction needs a stable causal cut -- agreement that no replica will ever
  again name a tombstone -- which is a sync-layer protocol (C6), not
  something one replica can decide. If you are using `Rga` for a long-lived
  high-churn document, this is the thing that will hurt.
- **`LwwRegister` discards concurrent writes silently.** If a lost edit is not
  acceptable, use `MvRegister` and resolve them yourself.
- **You need a total order, or a transaction across keys.** CRDTs give you
  convergence, not serializability. Nothing here will tell you that two
  updates conflicted at the application level.
- **You need this over a network today.** C6 does not exist. The core is
  transport-free on purpose and testable without one, but you are writing the
  sync layer.

## Limits

- **Elements and keys are `Sym`; register and `ORMap` values are `int`
  carriers.** A parametric element type needs that type to reach a
  `Hash`/`MapKey` instance, whose `mk-cmp` returns a comparator function
  pointer and can only be written in inline C -- which would cost the spice
  its interpreter coverage.
- **An `ORMap`'s value type must be carrier-shaped**: `int`, or a `defopaque`
  over `:int` such as stdlib's `MaxI`. A `defstruct` value does not compile --
  the emitter returns the aggregate from a function typed at the int64
  carrier. So "an `ORMap` of `PNCounter`s" is not available today, despite
  being the plan's motivating example; use a `defopaque` lattice as the value.
- **`Eq [GSet]` and `Eq [ORSet]` are size-only.** An element-wise comparison
  needs a set iterator the stdlib `Set` API does not expose the way the HAMT
  one does. The lattice law helpers are correspondingly weaker for those two
  types, which is why the OR-Set has a convergence fuzzer AND hand-written
  scenarios. `Eq [Rga]` is structural.
- **`ReplicaId` is a `defalias`, not a newtype**, so it buys no type safety and
  cannot be exported.
- **No benchmark yet** for HAMT join cost at realistic sizes.

## Tests

```sh
tur test tests/crdt
```

Eleven suites. Two are fuzzers -- `test_converge` (400 seeds over the OR-Set)
and `test_rga_converge` (300 seeds over the sequence) -- and both are
deterministic: the RNG is a pure-Turmeric LCG, replicas are rebuilt by
replaying the seed, and a failing case prints its seed.

The hand-written suites are not redundant with the fuzzers, and the files say
why. A convergence check cannot see a merge that converges on the WRONG
answer: removing the OR-Set merge's context test leaves 400 seeds green
(removes simply stop propagating), and dropping the clock max in `rga-merge`
leaves every seed green because each replica corrupts the id tree identically.
Both are caught by hand-written assertions instead.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/crdt-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/crdt>
