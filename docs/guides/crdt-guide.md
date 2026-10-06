---
title: Conflict-Free Replicated Data Types
category: Distributed Systems
description: Convergent data types for offline-first and peer-to-peer state -- counters, sets, registers, maps, and sequences that merge without coordination
audience: developers building collaborative, offline, or peer-to-peer applications
since: crdt v0.1.0
---

# tur-crdt Guide

Two replicas edit the same data while disconnected. When they reconnect, they
have to agree -- without a coordinator, without a lock, and without either
side's work being silently thrown away. A CRDT is a data type whose merge is
associative, commutative, and idempotent, which is exactly the set of
properties that makes "agree" fall out of arithmetic instead of out of a
protocol.

This guide walks the six things you'll do most often:

1. [Counters that count independently and converge](#1-counters)
2. [Sets that survive concurrent add and remove](#2-sets)
3. [Registers: last-writer-wins vs multi-value](#3-registers)
4. [Maps whose values are themselves CRDTs](#4-maps)
5. [Deltas: shipping changes, not full state](#5-deltas)
6. [Sequences: collaborative text editing](#6-sequences)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "crdt" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "crdt-v0.1.0"
           :subdir "spices/crdt"}
}
```

Then `tur fetch`. No CMake dependency -- `tur-crdt` is pure Turmeric. That
is deliberate: a single inline-C block would make the code unrunnable under
`tur --interpret`, and a CRDT's merge is precisely the thing worth exercising
on both back ends.

---

## The one idea

Every type here is a `JoinSemilattice`: `join` combines two states, and it
does not care what order you call it in, how you group the calls, or how
many times the same state arrives.

```turmeric no-check
(join a b)          ;; = (join b a)                  -- commutative
(join (join a b) c) ;; = (join a (join b c))         -- associative
(join a a)          ;; = a                           -- idempotent
```

Those three together are why a CRDT survives a network that reorders,
duplicates, and delays. You do not need an exactly-once link; you need a
merge that does not mind.

---

## 1. Counters

A `GCounter` is a map from replica to that replica's own total, joined by
pointwise maximum. Only the replica itself may bump its slot, which is what
makes the max a correct merge: a disagreement about a slot is always "one of
us has seen fewer updates", never a conflict.

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
    0))
```

```sweet-exp
defmodule demo
  import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value]
  defn alice [] : GCounter
    gcounter-bump(gcounter-new() replica((quote alice)) 3)
  defn bob [] : GCounter
    gcounter-bump(gcounter-new() replica((quote bob)) 5)
  defn main [] : int
    println $ gcounter-value $ join (alice) (bob)   ;; 8, in either order
    0
```

A `PNCounter` is two `GCounter`s -- one for increments, one for decrements
-- read as `P - N`. That is the standard trick for a counter that can go
down without either half ever decreasing.

```turmeric
(import crdt/counter :refer [PNCounter pncounter-new pncounter-inc
                             pncounter-dec pncounter-value])

(let [c (pncounter-inc (pncounter-new) (replica (quote alice)) 5)]
  (let [d (pncounter-dec c (replica (quote alice)) 2)]
    (println (pncounter-value d))))   ;; 3
```

```sweet-exp
import crdt/counter :refer [PNCounter pncounter-new pncounter-inc
                             pncounter-dec pncounter-value]
let [c pncounter-inc(pncounter-new() replica((quote alice)) 5)]
  let [d pncounter-dec(c replica((quote alice)) 2)]
    println $ pncounter-value d   ;; 3
```

---

## 2. Sets

Three designs, and the differences are the whole point:

- **`GSet`** only grows. Union is a semilattice for free. The baseline every
  other set is measured against.
- **`TwoPSet`** buys removal with a second grow-only set of tombstones. The
  trade is permanent: once removed, an element can never be re-added.
- **`ORSet`** buys re-adding back, and pays with a per-element dot store.
  Add wins over a concurrent remove; a remove that causally follows an add
  still removes it.

```turmeric
(defmodule demo
  (import crdt/orset :refer [ORSet orset-new orset-add orset-remove
                             orset-has? orset-count])

  (defn main [] : int
    (let [s (orset-remove
              (orset-add (orset-add (orset-new) (quote apple) (quote alice))
                         (quote pear) (quote alice))
              (quote apple))]
      (println (orset-count s)))     ;; 1 -- pear
    0))
```

```sweet-exp
defmodule demo
  import crdt/orset :refer [ORSet orset-new orset-add orset-remove
                             orset-has? orset-count]
  defn main [] : int
    let [s orset-remove
           orset-add(orset-add(orset-new() (quote apple) (quote alice))
                      (quote pear) (quote alice))
           (quote apple)]
      println $ orset-count s   ;; 1 -- pear
    0
```

A removal forgets the dots this replica has *observed* rather than writing
a tombstone. A concurrent add elsewhere carries a dot this replica never
saw, so it survives the merge. That is add-wins, and it is the reason
`crdt/causal` exists.

### When to pick which

| Type | Can remove? | Can re-add? | Grows under churn? |
|------|-------------|-------------|---------------------|
| `GSet` | No | -- | No (only grows with distinct adds) |
| `TwoPSet` | Yes | No | Yes (one tombstone per distinct element) |
| `ORSet` | Yes | Yes | No (context is a version vector, one entry per replica) |

---

## 3. Registers

`LwwRegister` picks a winner by HLC stamp. Every replica picks the same one,
which is what makes the join a join. The cost is that a concurrent write is
silently discarded -- right when the value is a fact one writer is
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
    0))
```

```sweet-exp
defmodule demo
  import crdt/register :refer [LwwRegister lww-new lww-set lww-value
                                MvRegister mv-new mv-set mv-values]
  defn main [] : int
    ;; Later stamp wins.
    println $ lww-value $ join
      lww-set(lww-new((quote alice)) 1 1000)
      lww-set(lww-new((quote bob)) 2 2000)   ;; 2
    ;; Concurrent writes: both survive.
    println $ mv-values $ join
      mv-set(mv-new((quote alice)) 1 1000)
      mv-set(mv-new((quote bob)) 2 1000)     ;; 2
    0
```

The `Hlc` (hybrid logical clock) keeps a wall component and a logical
counter, with the replica name as a final tiebreak so the order is total.
**Every operation takes `now` as a parameter** rather than calling a clock
-- that makes tests exact without a mock, and keeps the spice inline-C free.

---

## 4. Maps

`(ORMap V)` is the type that makes the rest compose. Its merge has two jobs
at once: decide which keys survive (the OR-Set problem, via dots and a
context) and merge the values of keys both sides hold.

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
    0))
```

```sweet-exp
defmodule demo
  import crdt/ormap :refer [ORMap ormap-new ormap-put ormap-get ormap-count]
  defn hi [] : (ORMap MaxI)
    :: (ormap-put (:: (ormap-new) (ORMap MaxI))
                   (quote alice) (quote temp) (:: 21 MaxI))
       (ORMap MaxI)
  defn lo [] : (ORMap MaxI)
    :: (ormap-put (:: (ormap-new) (ORMap MaxI))
                   (quote bob) (quote temp) (:: 19 MaxI))
       (ORMap MaxI)
  defn main [] : int
    println $ :: (:: (ormap-get (:: (join (hi) (lo)) (ORMap MaxI)) (quote temp))
                     MaxI)
                 int   ;; 21 -- MaxI's join
    0
```

`ormap-merge-with` passes the value merge explicitly instead, which is
strictly more general: two maps over one value type can merge differently.

**Every `ORMap`-valued binding needs an ascription.** `V` is phantom --
carried by no field -- so there is nothing to infer it from, and an
un-ascribed binding is declared at the int64 carrier while the constructor
returns a pointer. `tur check` passes and `cc` rejects it, and only on
clang >= 21, where `-Wint-conversion` is an error. It is a silent
macOS-only build failure if you forget.

---

## 5. Deltas

Shipping the full state on every change is what makes naive state-based
CRDTs unusable over a link. A delta mutator returns a small value the
receiver folds in with `apply-delta`:

```turmeric
(defmodule demo
  (import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value gcounter-delta-bump
                               apply-delta])

  (defn alice [] : GCounter
    (gcounter-bump (gcounter-new) (replica (quote alice)) 3))

  (defn bob [] : GCounter
    (gcounter-bump (gcounter-new) (replica (quote bob)) 5))

  (defn main [] : int
    ;; alice bumps again and ships only the delta; bob folds it in.
    (let [d (gcounter-delta-bump (alice) (replica (quote alice)) 2)]
      (println (gcounter-value (apply-delta (bob) d))))   ;; 10
    0))
```

```sweet-exp
defmodule demo
  import crdt/counter :refer [replica GCounter gcounter-new gcounter-bump
                               gcounter-value gcounter-delta-bump
                               apply-delta]
  defn alice [] : GCounter
    gcounter-bump(gcounter-new() replica((quote alice)) 3)
  defn bob [] : GCounter
    gcounter-bump(gcounter-new() replica((quote bob)) 5)
  defn main [] : int
    ;; alice bumps again and ships only the delta; bob folds it in.
    let [d gcounter-delta-bump(alice() replica((quote alice)) 2)]
      println $ gcounter-value $ apply-delta (bob) d   ;; 10
    0
```

The guarantee, asserted per type in `tests/crdt/test_delta.tur`: **applying
a sequence of deltas equals joining the full states.** Deltas also `join`
with each other, so a sender can buffer several and ship one.

### The delta TYPE differs for causal types

| State type | Delta type |
|------------|------------|
| `GCounter`, `PNCounter`, `GSet`, `TwoPSet`, `LwwRegister`, `MvRegister`, `Rga` | the state type |
| **`ORSet`, `ORMap`** | **`OrsetDelta` / `OrmapDelta`** |

A state's `DotContext` is a compacted version vector, so a delta carrying one
would claim every dot *below* the ones it names. An add-delta from alice@3
carrying `{alice: 3}` makes the receiver read its own untouched elements as
deliberately removed. So the causal deltas carry an exact `DotSet` of what
they retract, and they are a different type.

### Deltas require causal delivery

Applying a delta folds its exact dots into the receiver's version vector,
and that is lossless only while a replica's dots arrive without gaps. That
is the same assumption the version-vector compaction already makes; the
sync layer (C6, not yet started) is where it gets paid for.

---

## 6. Sequences

Every other type here merges values that have no position. A sequence
cannot: two replicas insert at "index 3" of a document they both believe
they are looking at, and an index is meaningless by the time the other side
sees it.

So an RGA never ships an index. Each element carries an id and the id of the
element it was inserted *after*, and the document order is a function of
that forest.

```turmeric
(defmodule demo
  (import crdt/rga :refer [Rga rga-new rga-append rga-insert rga-delete
                           rga-get rga-len rga-size])

  (defn main [] : int
    (let [d (rga-append (rga-append (rga-new) (quote alice) 72)
                        (quote alice) 105)]
      (println (rga-len d))      ;; 2
      (println (rga-get d 1)))   ;; 105 -- character codes
    0))
```

```sweet-exp
defmodule demo
  import crdt/rga :refer [Rga rga-new rga-append rga-insert rga-delete
                           rga-get rga-len rga-size]
  defn main [] : int
    let [d rga-append(rga-append(rga-new() (quote alice) 72)
                        (quote alice) 105)]
      println $ rga-len d      ;; 2
      println $ rga-get d 1    ;; 105 -- character codes
    0
```

Three consequences worth knowing before you use it:

1. **Ids are Lamport**, not per-replica, so a node's id always exceeds its
   origin's. The integration scan is correct only under that invariant.
2. **Concurrent runs do not interleave.** `abc` and `xyz` typed concurrently
   merge to one run then the other, never `axbycz`.
3. **Deletes are tombstones**, because a later insert may name a deleted
   element as its origin. `rga-size` therefore diverges from `rga-len` under
   churn.

Complexity, stated rather than implied: insert and delete are O(n), merge
is O(n*m). A production sequence CRDT indexes nodes by id and keeps a
balanced index for the position lookup. This one is written to be read.

---

## Working with affine states

**This is the gotcha that will bite first.** Every state here holds a Map
or Vec handle, so it is affine: `join` and every mutator *consume* their
argument. Two assertions over the same state need two states.

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

---

## When not to use this

- **`Rga` grows without bound under churn**, and it is the only type here
  that does. A document with 1000 characters typed and 1000 deleted reads
  `rga-len` 0 and `rga-size` 1000, and it is the second number that crosses
  the wire. Compaction needs a stable causal cut -- a sync-layer protocol
  (C6), not something one replica can decide.
- **`LwwRegister` discards concurrent writes silently.** If a lost edit is
  not acceptable, use `MvRegister` and resolve them yourself.
- **You need a total order, or a transaction across keys.** CRDTs give you
  convergence, not serializability. Nothing here will tell you that two
  updates conflicted at the application level.
- **You need this over a network today.** C6 (the sync layer) does not
  exist. The core is transport-free on purpose and testable without one, but
  you are writing the sync layer.

---

## Limits

- **Elements and keys are `Sym`; register and `ORMap` values are `int`
  carriers.** A parametric element type needs a `Hash`/`MapKey` instance
  whose `mk-cmp` returns a comparator function pointer and can only be
  written in inline C -- which would cost the spice its interpreter
  coverage.
- **An `ORMap`'s value type must be carrier-shaped**: `int`, or a
  `defopaque` over `:int` such as stdlib's `MaxI`. A `defstruct` value does
  not compile. So "an `ORMap` of `PNCounter`s" is not available today; use a
  `defopaque` lattice as the value.
- **`Eq [GSet]` and `Eq [ORSet]` are size-only.** An element-wise comparison
  needs a set iterator the stdlib `Set` API does not expose. The lattice law
  helpers are correspondingly weaker for those two types, which is why the
  OR-Set has a convergence fuzzer AND hand-written scenarios.
- **`ReplicaId` is a `defalias`, not a newtype**, so it buys no type safety
  and cannot be exported.

---

## Tests

```sh
tur test tests/crdt
```

Eleven suites. Two are fuzzers -- `test_converge` (400 seeds over the
OR-Set) and `test_rga_converge` (300 seeds over the sequence) -- and both
are deterministic: the RNG is a pure-Turmeric LCG, replicas are rebuilt by
replaying the seed, and a failing case prints its seed.

---

## See also

- [API reference](api/)
- [README](../../spices/crdt/README.md) -- the full type catalog and status table
- [Design plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/crdt-spice-plan.md) -- the six-phase roadmap
- [Lattice typeclass guide](https://turmeric-lang.com/docs/html/guides/lattice-guide.html) -- `JoinSemilattice`, `BoundedJoin`
