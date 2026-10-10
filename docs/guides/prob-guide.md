---
title: Probabilistic Data Structures
category: Data
description: Bloom and Cuckoo filters, HyperLogLog, Count-Min Sketch, Top-K and t-digest -- answer "seen it?", "how many?", "how often?" and "what's the p99?" over a stream in fixed memory, with merge for sharded data
audience: developers summarising large or unbounded streams -- logs, events, metrics, dedup -- where an exact answer costs more memory than the question is worth
since: prob v0.1.0
---

# tur-prob Guide

You have a stream of events and four questions about it. Have I seen this id
before? How many distinct ids are there? Which are the most frequent, and how
often does each occur? What is the 99th-percentile latency? Answered exactly,
each one needs memory proportional to the stream. Answered approximately, each
needs a fixed, small amount -- chosen up front -- and a bounded, *known* error.

`tur-prob` is that trade, one structure per question. This guide covers:

1. [Choosing a structure](#1-choosing-a-structure)
2. [Bloom filter: have I seen it?](#2-bloom-filter)
3. [Cuckoo filter: ...and can I forget it?](#3-cuckoo-filter)
4. [HyperLogLog: how many distinct?](#4-hyperloglog)
5. [Count-Min Sketch and Top-K: how often?](#5-count-min-sketch-and-top-k)
6. [t-digest: what's the p99?](#6-t-digest)
7. [Merging sketches from many shards](#7-merging)

Read [Compiling](#compiling----read-this-first) before you trust any number
these produce.

Everything below is one runnable program:
`examples/01_tour.tur`. The figures quoted are what it prints.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "prob" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "main"
           :subdir "spices/prob"}
}
```

Then `tur fetch`. Pin `:ref` to a commit SHA, or to a `prob-v*` tag once one is
cut; there is none yet. Pure Turmeric apart from one line of inline C in
`prob/hll` (a count-leading-zeros), so the HyperLogLog does not run under
`tur --interpret` and the other five do.

---

## Compiling -- read this first

**Compile with `-fwrapv`, or the sketches can silently return wrong answers.**

Every structure here hashes with 64-bit multiplication that is *meant* to wrap
around (a splitmix64 finalizer). Turmeric's emitted C does that arithmetic on
signed `int64_t`, and signed overflow is undefined behavior in C. The default
flags (`-O2 -std=c99 -Wall -fno-strict-aliasing`) do not include `-fwrapv`, so
the optimizer is entitled to assume it never happens -- and sometimes acts on
that.

Measured here, on the program in `examples/01_tour.tur`: with the default flags
the Count-Min Sketch returned **0 for every key**, including a key added 20,000
times, while the *same source* with `-fwrapv` returned 20000, 2857 and 133. The
spice's own test suite passes either way, so a green `tur test` does not rule
this out. Whether it bites depends on what surrounds the call, which is what
makes it dangerous: adding one extra `cms-estimate` line to a function changed
the answers of the lines before it. Compiling the emitted C at `-O0` is also
correct, and UBSan reports the overflowing multiplications in `prob/hash`.

Until the compiler adds `-fwrapv` to its defaults, set it yourself:

```sh
TUR_CC_FLAGS="-O2 -std=c99 -Wall -fno-strict-aliasing -fwrapv" tur run my-app.tur
```

`TUR_CC_FLAGS` replaces the default flags rather than adding to them, so repeat
the defaults as above. This is a compiler issue, not a `tur-prob` one;
it is filed as
[emitted-c-signed-overflow-no-fwrapv](https://github.com/turmeric-lang/turmeric/blob/main/docs/reported/emitted-c-signed-overflow-no-fwrapv.md).
Any program of your own that hashes with wrapping integer arithmetic is exposed
in the same way.

---

## 1. Choosing a structure

| You ask | Use | Error | Memory | Merge |
|---|---|---|---|---|
| "Have I seen `x`?" | `Bloom` | false positives, never false negatives | `m` bits | OR -- idempotent |
| ...and I need to remove `x` | `Cuckoo` | false positives, never false negatives (until you delete something you did not insert) | `num-buckets x 4` fingerprints | re-insert -- not idempotent |
| "How many distinct?" | `Hll` | about `1.04 / sqrt(2^p)` relative | `2^p` registers | max -- idempotent |
| "How often did `x` occur?" | `CountMin` | over-counts, never under-counts | `d x w` counters | add -- not idempotent |
| "Which `k` are most frequent?" | `TopK` | **exact** | grows with distinct keys | add -- not idempotent |
| "What is quantile `q`?" | `TDigest` | relative to rank; best at the tails | about `compression` centroids | combine -- not idempotent |

Three things hold for all of them:

- **Keys are `:int`.** There is no string or struct API; hash your own key to an
  `int` first. (`TDigest` takes `:float` values.)
- **Sketches are move-only.** Each is a struct that owns a `Vec`. Free it with
  its `*-free`; take it as `^borrow` in your own functions so it is not moved
  away. `merge` returns a *new* sketch and leaves both inputs valid.
- **Version 0.1 stores a whole `int` per bit.** A `Bloom` of `m = 65536` bits
  occupies 512 KB, not 8 KB; `Hll`, `CountMin` and `Cuckoo` likewise use 8 bytes
  per register, counter or slot. The sizes in the table are element counts. A
  packed representation is a noted follow-up.

---

## 2. Bloom filter

A bit array and `k` hash functions. Inserting sets `k` bits; a lookup checks
them. If any is clear the key was *definitely* never inserted; if all are set
it *probably* was.

```turmeric
(import prob/bloom :refer [Bloom bloom-new bloom-free bloom-insert! bloom-maybe?])

(let [bf (bloom-new 65536 7)]      ;; m = 65536 bits, k = 7 hashes
  (bloom-insert! bf 42)
  (bloom-maybe? bf 42)             ;; true
  (bloom-maybe? bf 43)             ;; false, with high probability
  (bloom-free bf))
```

With 5,000 members in 65,536 bits and 7 hashes (`examples/01_tour.tur`):

- all 5,000 members are found -- no false negatives, by construction;
- 23 of 10,000 never-inserted keys are reported present: **0.23%**, against
  `(1 - e^(-kn/m))^k = 0.20%` predicted.

Sizing: for `n` expected members and a false-positive rate `p`, take
`m = -n ln p / (ln 2)^2` bits and `k = (m / n) ln 2` hashes. `m` should be a
power of two, so the index reduction is a mask.

`bloom-count` is the number of set bits (27,083 of 65,536 here). Once it passes
about half of `m` the filter is saturated and the false-positive rate climbs
fast -- watch it as a health gauge.

---

## 3. Cuckoo filter

A Bloom filter cannot forget. A Cuckoo filter stores small *fingerprints* of its
keys in a bucket array, so it can delete one -- at the cost of a bounded
relocation loop when inserting into a crowded filter.

```turmeric
(import prob/cuckoo :refer [Cuckoo cuckoo-new cuckoo-free cuckoo-insert!
                            cuckoo-maybe? cuckoo-delete!])

(let [cf (cuckoo-new 4096)]        ;; 4096 buckets x 4 slots
  (cuckoo-insert! cf 42)           ;; true; false means the filter is FULL
  (cuckoo-maybe? cf 42)            ;; true
  (cuckoo-delete! cf 42)           ;; true: removed
  (cuckoo-maybe? cf 42)            ;; false
  (cuckoo-free cf))
```

12,000 keys into 4,096 buckets (a 73% load; 16,384 slots) in the tour:

- every insert succeeds, and 11,704 fingerprints are stored. The 296 difference
  are keys whose 8-bit fingerprint was already in one of their two buckets and
  were absorbed -- an inherent cost of 8-bit fingerprints, which is also where
  the false positives come from;
- 443 of 10,000 absent keys are reported present: **4.4%**. It rises with load
  and falls only by widening the fingerprint, which `tur-prob` does not offer:
  8 bits is fixed.

Rules that differ from Bloom:

- **`cuckoo-insert!` returns `false` when full.** Check it. A filter keeps
  working up to roughly 95% load; size `num-buckets` (a power of two) for the
  union you expect.
- **Only delete what you inserted.** Deleting a key that was never inserted can
  remove a *different* key's matching fingerprint and create a false negative.
- **Inserting the same key twice stores it once**, so one delete removes it.
  That is a set, not a multiset.

---

## 4. HyperLogLog

Counts distinct keys in `2^p` small registers. Each key is hashed; the low `p`
bits choose a register, which remembers the longest run of leading zeros it has
seen in the rest. The harmonic mean of the registers estimates the count.

```turmeric
(import prob/hll :refer [Hll hll-new hll-free hll-add! hll-cardinality])

(let [h (hll-new 14)]              ;; p = 14: 16384 registers
  (hll-add! h 42)
  (hll-add! h 42)                  ;; a repeat changes nothing
  (hll-cardinality h)              ;; ~ 1.0   (a :float)
  (hll-free h))
```

`p` is in `[4, 16]`; the standard error is about `1.04 / sqrt(2^p)`, so `p = 14`
gives roughly 0.8%. In the tour, two overlapping sets of 6,000 ids
(`[0, 6000)` and `[4000, 10000)`) come back as 5,995 and 5,959, and their merge
as **9,965 against a true 10,000** -- inside one standard error, with no list of
ids kept.

Small counts use a linear-counting correction. `hll-cardinality` returns a
`:float`; round it yourself.

---

## 5. Count-Min Sketch and Top-K

A `d x w` grid of counters. Each key hashes to one counter per row; adding
increments all `d`; the estimate is the **minimum** of the `d` counters, so it
can only be too high, never too low.

```turmeric
(import prob/cms :refer [CountMin cms-new cms-free cms-add! cms-estimate])

(let [cm (cms-new 5 1024)]         ;; d = 5 rows, w = 1024 columns
  (cms-add! cm 42)
  (cms-add! cm 42)
  (cms-estimate cm 42)             ;; 2 (or a little more)
  (cms-estimate cm 7)              ;; 0 (or a little more)
  (cms-free cm))
```

The over-count is bounded: with probability `1 - e^(-d)` it is at most
`(e / w) x N` for a stream of `N` events. Choose `w` (a power of two) for the
error and `d` for the confidence; `w` is the one that costs memory.

On a heavy-tailed stream -- key `k` occurring `20000 / k` times, 200 keys,
about 117,000 events -- the estimates were **exact**: 20000 for key 1, 2857 for
key 7, 133 for key 150, and 0 for a key never seen. That is the favourable case
(200 keys in 1,024 columns rarely collide); a stream with millions of distinct
keys in the same sketch would over-count. `cms-inner-product` gives the same
kind of estimate for the size of a join between two streams.

For the *heaviest* keys, a `TopK` tracks them exactly:

```turmeric
(import prob/topk :refer [TopK topk-new topk-free topk-add! topk-list topk-count])

(let [tk (topk-new 3)]
  (topk-add! tk 42)
  (topk-add! tk 42)
  (topk-add! tk 7)
  (let [top (topk-list tk)]         ;; flat Vec: [count, key, count, key, ...]
    (vec-get top 0)                 ;; 2   -- the highest count
    (vec-get top 1)                 ;; 42  -- its key
    (vec-free top))                 ;; the caller owns the list
  (topk-free tk))
```

In the tour it reports keys 1, 2, 3 with counts 20000, 10000, 6666 -- exactly.

**`TopK` is not a sketch.** It keeps one entry per distinct key, so its memory
grows with the number of distinct keys, unbounded. Its `k` only limits what
`topk-list` returns. That makes it the right tool when the distinct set is
moderate and the wrong one over an unbounded key space; there, use a
`CountMin` to estimate and keep your own bounded candidate set. A
memory-bounded, sketch-backed variant is a noted follow-up.

---

## 6. t-digest

Streaming quantiles: keep a few hundred weighted centroids, finest at the tails
where the question is usually asked (the p99, the p99.9), coarser in the middle.

```turmeric
(import prob/tdigest :refer [TDigest tdigest-new tdigest-free tdigest-add!
                             tdigest-quantile tdigest-count])

(let [td (tdigest-new 100.0)]      ;; compression: more = more centroids = more accurate
  (tdigest-add! td 12.5)           ;; values are :float
  (tdigest-add! td 40.0)
  (tdigest-quantile td 0.5)        ;; :float; 0.0 if the digest is empty
  (tdigest-free td))
```

The values `0..9999` at compression 100 are summarised by **74 centroids**:

| quantile | true | t-digest |
|---|---|---|
| p1 | 100 | 111 |
| p50 | 4999 | 4936 |
| p99 | 9899 | 9885 |
| p99.9 | 9989 | 9988 |

The error is on the order of 1% of the value range at the median and shrinks
toward the tails, which is the design: rank-accurate where it matters. Raise the
compression for a tighter fit, at proportionally more centroids.

---

## 7. Merging

The reason to want a sketch rather than a list is that sketches from different
shards combine into the sketch of the union, without a second pass over the
data:

```turmeric
(let [merged (hll-merge shard-a shard-b)]   ;; a NEW Hll; both inputs stay valid
  (hll-cardinality merged))
```

Each `*-merge` requires matching shape -- the same `m` and `k`, `p`, `d` and `w`,
`num-buckets`, or compression -- and returns a fresh sketch. What differs is
whether merging is *safe to repeat*, which matters when the same data can reach
you twice (at-least-once delivery, overlapping windows):

| Structure | `merge` is | Merge a sketch with itself |
|---|---|---|
| `Bloom` | bitwise OR | unchanged |
| `Hll` | register-wise max | unchanged |
| `CountMin` | counter-wise sum | **doubles every count** |
| `TDigest` | combine and re-compress | **doubles every weight** (quantiles unchanged) |
| `TopK` | add counts | **doubles every count** |
| `Cuckoo` | re-insert one into the other | not a join; a full result can drop fingerprints |

`Bloom`, `Hll`, `CountMin` and `TDigest` carry `JoinSemilattice` instances, so
they compose with the lattice combinators in `stdlib/typeclass-lattice`; the law
checks exempt idempotence for `CountMin` and `TDigest` by name. `Cuckoo` and
`TopK` deliberately do not. In practice: **`Bloom` and `Hll` tolerate duplicate
delivery; the counting structures do not**, so deduplicate shard deliveries
before merging a `CountMin`, `TopK` or `TDigest`.

---

## Limits

- **Integer keys only.** Hash strings and structs to an `int` yourself.
- **8 bytes per bit/register/counter/slot** in 0.1 (see section 1).
- **`Cuckoo` fingerprints are 8 bits**, fixed; the false-positive floor is a few
  percent at high load.
- **`TopK` memory is unbounded** in the number of distinct keys.
- **`Hll` is not interpreter-compatible** (one inline-C line); the rest are.
- **Needs `-fwrapv`** -- see [Compiling](#compiling----read-this-first).

---

## Tests

```sh
cd spices/prob
tur test tests/prob    # 8 tests, all pass
```

---

## See also

- [Conflict-free replicated data types](crdt-guide.html) -- the same merge
  discipline, for exact state rather than sketches.
- `examples/01_tour.tur` -- every number in this guide.
