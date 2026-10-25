# Changelog

## 0.3.0

### Changed (breaking)

- `ReplicaId` is a newtype over `Sym`, in a new module `crdt/replica`, and
  every type takes its replica as one: `mk-dot`, `dot-replica`,
  `dotctx-next`, `hlc-zero`, `hlc-replica`, `lww-new`, `mv-new`,
  `orset-add`, `orset-delta-add`, `ormap-put`, `ormap-delta-put`,
  `rga-insert`, `rga-append` and `rga-delta-insert`, as well as the
  counters. A bare `Sym` in a replica position is now a type error, so the
  element or key that `orset-add` and `ormap-put` also take can no longer be
  swapped with it unnoticed (`errors/run.sh` pins both). `replica` moved
  from `crdt/counter` to `crdt/replica`; `replica-sym` is new. Until now
  `ReplicaId` was a `defalias`, because a `defopaque` over `:Sym` did not
  compile (turmeric `defopaque-over-sym-skips-the-ptr-bridge`, fixed
  2026-09-16).

### Fixed

- `orset-apply` of an add delta costs the delta, not the state: it rebuilt
  every element's dot map even when the delta retracted nothing. A
  one-element add on a 65536-element set took 658 ms; it takes 9 us.

### Added

- `bench/join.tur` and `bench/run.sh`: the HAMT join benchmark
  crdt-spice-plan section 7 asked for, at 16 to 65536 entries. The README's
  **Performance** section has the numbers.

### Internal

- `gset-add` inserts through the typed `set-add1` rather than the
  explicit-hash `set-add` macro (turmeric
  `set-add-elem-hash-disagrees-with-set-member`, fixed 2026-09-16).

## 0.2.0

- `DeltaCRDT` with delta mutators for every C1-C3 type (crdt-spice-plan
  C4), and `crdt/rga`, the replicated sequence (C5). turmeric-spices #76.

## 0.1.0

- Counters, the causal core, `GSet` / `TwoPSet` / `ORSet`, the HLC, both
  registers and `ORMap` (crdt-spice-plan C1-C3). turmeric-spices #67, #68.
