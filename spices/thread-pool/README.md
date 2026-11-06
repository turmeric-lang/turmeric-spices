# tur-thread-pool

A bounded POSIX worker-thread pool for Turmeric. Workers pull work items off a
fixed-capacity ring buffer and hand each to a user callback. Producers get
cooperative back-pressure: `pool-submit` blocks while the queue is full rather
than dropping work.

Since **0.2.0** the canonical surface is **typed and linear**: `(Pool T)` is an
opaque, `:linear` handle parameterised by its work-item type. The type system
enforces that the pool is consumed exactly once and that items match the
callback's type. A raw `ptr<void>` C-interop hatch remains for FFI consumers
(and is what the `tur-httpd` adapter uses).

> The full prose guide lives in the `turmeric` repo at
> `docs/guides/thread-pool-guide.md`; it should be updated there to match this
> 0.2.0 surface (that file cannot be edited from a `turmeric-spices` session).

## Modules

| Module | Provides |
|---|---|
| `thread-pool/pool`     | `(Pool T)`, `pool-new` / `pool-submit` / `pool-stop`, `pool-try-submit` + introspection, and the raw hatch |
| `thread-pool/scope`    | `with-pool` scope macro |
| `thread-pool/future`   | `(Future R)`, `pool-submit-future` / `future-await` / `future-try` / `future-cancel` |
| `thread-pool/parallel` | `(TList A)` + `pool-map` fan-out convenience |

## Typed, linear pool (`thread-pool/pool`)

| Function | Signature | Description |
|---|---|---|
| `pool-new`    | `[T] [size : int callback : (fn [T] void)] : (Pool T)` | Spawn `size` workers (clamped `>= 1`); `callback` runs on each item. Returns a linear `(Pool T)`. |
| `pool-submit` | `[T] [^borrow pool : (Pool T) item : T] : void`        | Enqueue a typed item; blocks under back-pressure. Ownership of `item` moves to the worker. |
| `pool-stop`   | `[T] [pool : (Pool T)] : void`                          | Drain queued items, join workers, free. **Consumes** the handle. |

The linear discipline is enforced at compile time:

- forgetting `pool-stop` is `TUR-E0100` (linear value dropped);
- a second `pool-stop` is `TUR-E0101` (used after consume);
- submitting the wrong item type (`cstr` into a `(Pool int)`) is `TUR-E0001`.

```turmeric
(defmodule example
  (import thread-pool/pool :refer [Pool pool-new pool-submit pool-stop]))

(defn consume [x : int] : void
  ;; ... do work with x ...
  nil)

(defn run [] : int
  (let [p (pool-new 8 consume)]
    (pool-submit p 1)
    (pool-submit p 2)
    (pool-stop p)        ;; required: the pool is linear
    0))
```

```sweet-exp
#lang sweet-exp
defmodule example
  (import thread-pool/pool :refer [Pool pool-new pool-submit pool-stop])

defn consume [x : int] : void
  ;; ... do work with x ...
  nil

defn run [] : int
  let [p pool-new(8 consume)]
    pool-submit(p 1)
    pool-submit(p 2)
    pool-stop(p)         ;; required: the pool is linear
    0
```

### Capacity

The ring buffer holds `max(size * 4, 64)` items (4x worker headroom, floor 64).

### Ownership and lifetimes

- **Items.** `pool-submit` consumes the item; the worker callback owns it. The
  submitter must not touch it afterward.
- **Callback.** A `(fn [T] void)` closure; it must remain valid for the pool's
  lifetime (bind it in a scope that outlives `pool-stop` -- naming it as a
  top-level `defn` is the simplest way).
- **Shutdown.** Workers drain every successfully-submitted item before exiting,
  so each enqueued item is delivered exactly once. Do not submit after
  `pool-stop` has begun -- join the producer first.

## Scoped pools (`thread-pool/scope`)

`with-pool` binds a linear pool, runs the body, and runs `pool-stop` on the way
out -- you cannot forget it, and a manual `pool-stop` inside the body is a
double-consume error.

```turmeric
(import thread-pool/scope :refer [with-pool])

(with-pool [p (pool-new 8 consume)]   ;; callback must be a named defn here
  (pool-submit p 1)
  (pool-submit p 2))                   ;; pool-stop p runs automatically
```

```sweet-exp
(import thread-pool/scope :refer [with-pool])

with-pool [p pool-new(8 consume)]   ;; callback must be a named defn here
  pool-submit(p 1)
  pool-submit(p 2)                   ;; pool-stop p runs automatically
```

`with-pool` guarantees cleanup on **normal** completion. It does not install an
unwinding finalizer: an uncaught panic propagating through the scope aborts the
process (the OS reclaims the threads and memory). Wrap the body in your own
`catch-unwind` if you need a pool to survive a recoverable panic.

## Returning results: futures (`thread-pool/future`)

`pool-submit-future` runs `work(item)` on a worker and hands back a linear
`(Future R)` you can await. This replaces hand-rolled mutex + result-slot
plumbing for fan-out / collect.

| Function | Signature | Description |
|---|---|---|
| `pool-submit-future` | `[T R] [^borrow pool : (Pool T) item : T work : (fn [T] R)] : (Future R)` | Submit `(item, work)`; get a future for the result. |
| `future-await`  | `[R] [fut : (Future R)] : R`           | Block for the result; **consumes** the future. |
| `future-try`    | `[R] [^borrow fut : (Future R)] : (Option R)` | Non-blocking peek; borrows (poll freely). |
| `future-cancel` | `[R] [fut : (Future R)] : void`        | Drop without awaiting; **consumes** the future. Does NOT stop in-flight work. |

The future owns a heap cell (mutex + condvar + slot) shared by the worker and
the awaiter via a reference count; whichever side finishes last frees it, so a
cancelled-but-still-running task is safe. `work` is invoked later on a worker
thread, so it must outlive the await (`pool-map` arranges this for you).

### Fan-out: `pool-map` (`thread-pool/parallel`)

```turmeric
(import thread-pool/parallel :refer [TList tlist-nil tlist-cons pool-map])

(defn square [x : int] : int (* x x))

;; submit every item as a future, await all, collect in order
(pool-map pool
          (tlist-cons 1 (tlist-cons 2 (:: (tlist-nil) (TList int))))
          square)        ;; => (TList int) of [1 4]
```

```sweet-exp
(import thread-pool/parallel :refer [TList tlist-nil tlist-cons pool-map])

defn square [x : int] : int {x * x}

;; submit every item as a future, await all, collect in order
pool-map pool
  tlist-cons(1 tlist-cons(2 ::(tlist-nil() (TList int))))
  square         ;; => (TList int) of [1 4]
```

`pool-map` submits all items before awaiting any, so the work runs in parallel
across the pool, and returns results in input order.

## Backpressure: non-blocking submit + introspection (`thread-pool/pool`)

| Function | Signature | Description |
|---|---|---|
| `pool-try-submit` | `[T] [^borrow pool : (Pool T) item : T] : (Result Unit FullError)` | Enqueue without blocking; `(err (PoolFull cap cap))` if the ring is full (the item is not enqueued -- you still own it). |
| `pool-pending`    | `[T] [^borrow pool : (Pool T)] : int` | Items currently queued. |
| `pool-workers`    | `[T] [^borrow pool : (Pool T)] : int` | Live worker count. |
| `pool-queue-cap`  | `[T] [^borrow pool : (Pool T)] : int` | Ring capacity (`max(size*4, 64)`). |

```turmeric
(let [r (pool-try-submit p item)]
  (if (ok? r)
    :accepted
    (match (err-val r) (PoolFull pending cap) :shed-load)))
```

```sweet-exp
let [r pool-try-submit(p item)]
  if ok?(r)
    :accepted
    (match (err-val r) (PoolFull pending cap) :shed-load)
```

## Raw C-interop hatch (`thread-pool/pool`)

For "work is already `ptr<void>`-ABI" cases (C library interop; the `httpd/pool`
adapter), the raw entry points keep a stable, non-generic `ptr<void>` ABI and a
raw `void (*)(void *item)` callback. They are not linear -- the caller manages
the handle's lifetime.

| Function | Signature |
|---|---|
| `pool-new-raw`    | `[size : int callback : ptr<void>] : ptr<void>` |
| `pool-submit-raw` | `[pool : ptr<void> item : ptr<void>] : void` |
| `pool-stop-raw`   | `[pool : ptr<void>] : void` |

```turmeric
(defn work-cb [item : ptr<void>] : void
  ```c
  int *v = (int *)item; /* ... */ free(v);
  ```)
(defn work-cb-ptr [] : ptr<void>
  ```c
  extern void example__work_hycb(void *);
  return (void *)example__work_hycb;
  ```)

(let [pool (pool-new-raw 4 (work-cb-ptr))]
  ;; (pool-submit-raw pool item) ...
  (pool-stop-raw pool))
```

````sweet-exp
#lang sweet-exp
defn work-cb [item : ptr<void>] : void
  ```c
  int *v = (int *)item; /* ... */ free(v);
  ```
defn work-cb-ptr [] : ptr<void>
  ```c
  extern void example__work_hycb(void *);
  return (void *)example__work_hycb;
  ```

let [pool pool-new-raw(4 work-cb-ptr())]
  ;; (pool-submit-raw pool item) ...
  pool-stop-raw(pool)
````

## Known limitations (turmeric main, 0.24.x)

- **Opaque item types.** A user-defined `(defopaque Tag ...)` cannot currently
  be used directly as the `(Pool T)` item type -- `pool-submit` rejects it with
  `TUR-E0001` ("expected tyvar"), because the bare-tyvar value parameter does
  not unify with a concrete nominal opaque when `T` is also bound through the
  `(Pool T)` wrapper. `int`, `cstr`, `ptr<void>`, and scalar carriers work;
  carry an opaque as a `ptr<void>` to a heap cell until this is fixed (see the
  `ptr<void>` identity case in `tests/thread-pool/test_pool.tur`).
- **`with-pool` callbacks.** A macro argument cannot itself be a typed inline
  `(fn [x : int] ...)`; name the callback as a top-level `defn` and pass the
  name (as the examples do).

## Tests

```sh
cd spices/thread-pool
tur test tests/thread-pool                 # functional suite (5 files)
errors/run.sh $(command -v tur)            # linear/type rejections
```

The functional suite covers basic submit + drain, back-pressure, size
clamping, stop-with-queue-non-empty, multiple distinct `(Pool T)` in one
translation unit, the raw hatch, `pool-try-submit` + accessors, `with-pool`
(scope-exit and nested), futures (fan-out await, try, cancel), and `pool-map`.
The compile-FAIL fixtures live in `errors/`, not under `tests/` -- `tur test`
recurses, and would build and run each of them as an ordinary test and count
it as a failure. `errors/run.sh` (which CI runs) asserts instead that each is
rejected, and for the right reason: forgetting `pool-stop` (`TUR-E0100`),
double `pool-stop` (`TUR-E0101`), and a wrong-typed submit (`TUR-E0001`).

## Scope

In scope for 0.2.0: a single bounded FIFO queue, a typed/linear handle, futures
for result collection, `pool-map` fan-out, non-blocking submit, and the raw
C-interop hatch. Out of scope: work-stealing, priority lanes, per-task
deadlines, worker CPU pinning, a full async runtime, cross-pool transfer, and
cancellation propagation into in-flight work.
