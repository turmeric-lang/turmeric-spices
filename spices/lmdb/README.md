# tur-lmdb

[LMDB](https://github.com/LMDB/lmdb) bindings for Turmeric: an embedded,
memory-mapped, transactional key-value store. Open a file, put bytes in by
key, read them back after a restart -- no server, no SQL.

## Overview

`tur-lmdb` is a Tier 2 spice: LMDB 0.9.33 is two C files, vendored verbatim
under `c/lmdb/` (OpenLDAP Public License, see `c/lmdb/VENDORED.md`) and
compiled into your program, so there is nothing to fetch.

The API follows LMDB's own shape -- environments, transactions, named
databases, cursors -- typed the Turmeric way:

- `Env`, `Txn` and `Cursor` are `:linear` handles: a leaked or double-closed
  one is a compile-time error (TUR-E0100 / TUR-E0101).
- Fallible calls return `(Result T LmdbError)`; "not there" is `(Option T)`
  or `false`, never an error.
- Scoped transactions (`with-read-txn`, `with-write-txn`) commit on `Ok` and
  abort on `Err` or a panic; the callback only borrows the `Txn`.

What LMDB gives you: MVCC (readers never block the writer, and see one
consistent snapshot each), sorted keys and range scans, duplicate-sorted
databases (several values per key), multi-key atomic writes, and no recovery
step after a crash.

It is a binding, not a dictionary: a generic key-value API over several
backends is the job of a future `kv` spice, with `lmdb` as one adapter.

## Install

```turmeric no-check
:spices #map{
  "lmdb" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "lmdb-v0.1.0"
              :subdir "spices/lmdb"}
}
```

## Quick start

```turmeric
(import lmdb/error :refer [LmdbError])
(import lmdb/env   :refer [env-opts env-open env-close])
(import lmdb/txn   :refer [Txn with-read-txn with-write-txn])
(import lmdb/dbi   :refer [dbi-main])
(import lmdb/ops   :refer [get put!])

(defn demo [] : int
  (let [env (ok-val (env-open "app.mdb" (env-opts)))
        w   (with-write-txn env
              (fn [^borrow t : Txn] : (Result nil LmdbError)
                (put! t (? (dbi-main t)) "hello" "world")))
        v   (with-read-txn env
              (fn [^borrow t : Txn] : (Result (Option cstr) LmdbError)
                (ok (get t (? (dbi-main t)) "hello"))))]
    (env-close env)
    (if (and (ok? v) (some? (ok-val v))) 0 1)))
```

```sweet-exp
#lang sweet-exp
import lmdb/error :refer [LmdbError]
import lmdb/env   :refer [env-opts env-open env-close]
import lmdb/txn   :refer [Txn with-read-txn with-write-txn]
import lmdb/dbi   :refer [dbi-main]
import lmdb/ops   :refer [get put!]

defn demo [] : int
  let [env ok-val(env-open("app.mdb" env-opts()))
       w   with-write-txn(env
             (fn [^borrow t : Txn] : (Result nil LmdbError)
               put!(t ?(dbi-main(t)) "hello" "world")))
       v   with-read-txn(env
             (fn [^borrow t : Txn] : (Result (Option cstr) LmdbError)
               ok(get(t ?(dbi-main(t)) "hello"))))]
    env-close(env)
    if {ok?(v) and some?(ok-val(v))} 0 1
```

## Modules

| Module | What it holds |
| --- | --- |
| `lmdb/error` | `LmdbError` (`MapFull`, `KeyTooLong`, `ReadersFull`, `DbsFull`, `TxnFull`, `NotADatabase`, `Incompatible`, `ReadOnly`, `Io`) |
| `lmdb/env` | `Env`, `EnvOpts` (`env-opts` and the `opts-*` setters), `env-open` / `env-close`, map size, `env-info`, `env-stat`, `env-sync`, `env-copy` (hot backup), `env-reader-check` |
| `lmdb/txn` | `Txn`; raw `txn-begin` / `txn-commit` / `txn-abort` / `txn-begin-nested`; scoped `with-read-txn`, `with-write-txn`, `with-nested-txn`, `with-write-txn-growing` |
| `lmdb/dbi` | `Dbi`, `DbiFlags` (`dbi-create`, `dbi-dup-sort`, ... combined with `dbi-flags+`), `dbi-open`, `dbi-main`, `dbi-stat`, `dbi-drop`, `dbi-delete` |
| `lmdb/ops` | `get`, `has?`, `put!`, `put-new!` (put-if-absent), `put-append!` (bulk load), `del!`, `del-dup!` |
| `lmdb/cursor` | `Cursor`, `with-cursor`, moves (`cursor-first` ... `cursor-seek` for range scans, the `-dup` moves), `cursor-fold`, `cursor-put!`, `cursor-del!` |
| `lmdb/bytes` | the same operations over stdlib `Bytes`, for binary keys and values; `put-reserve!` |

## Things to know

**Map size.** An environment has a fixed maximum size, set when it opens:
1 GiB by default on Linux and macOS, where an unused map costs no disk (a
1 GiB map holding one record is a 12 KiB file), and 64 MiB on Windows,
where LMDB 0.9.33 extends the file to the whole map size at open. A write
past it is `MapFull`; nothing grows the map behind your back. Call
`env-set-map-size!` between transactions, or use `with-write-txn-growing`,
which doubles the map up to a maximum and reruns the transaction.

**One file.** `env-open` makes `path` a single data file with `path-lock`
beside it (`MDB_NOSUBDIR`). `opts-subdir` gives LMDB's directory layout, for
files shared with LMDB's own tools. Files are not portable across word size
or byte order.

**Copies out.** Every key and value handed back is a fresh heap copy:
LMDB's pointers die with their transaction. A returned `cstr` is the
caller's, as with stdlib's "caller frees" functions -- free it, or hand it
to `string/adopt-cstr`. Returned `Bytes` are linear, so the checker makes
you `bytes-free` them.

**Keys** are at most 511 bytes (`env-max-key-size`); a longer one is
`KeyTooLong` before LMDB sees it. A `cstr` is stored without its NUL, so it
and a `Bytes` of the same bytes are the same key.

**Threads.** Every env is opened `MDB_NOTLS`, so a read transaction is not
tied to its OS thread. A write transaction is: LMDB's writer lock must be
released by the thread that took it, so do not `await` inside
`with-write-txn`. One writer at a time per environment; readers never wait.

**Cursors** must be closed before their transaction ends (LMDB frees a write
transaction's cursors at its end). `with-cursor` does that, panic included.
For large walks use `cursor-fold` (a loop) rather than recursion.

**Durability.** Commits fsync by default; `opts-sync` with `(SyncNoMetaSync)`
or `(SyncNone)` trades that away for caches and tests. `MDB_WRITEMAP` is
never used.

## Benchmark

`bench/` is a separate project comparing this binding with `spices/sqlite`
on the same key-value work (100 000 keys of 9 bytes, 100-byte values; fsync
off on both: `SyncNone`, and `synchronous=OFF` + WAL). Median of three runs,
Linux x86-64 (4-core Xeon @ 2.8 GHz, gcc 13, ext4), 2026-10-25:

| Workload | lmdb ns/op | sqlite ns/op |
| --- | ---: | ---: |
| load: 100 000 puts in one transaction | 705 | 1 519 |
| get: 100 000 point reads, scrambled order | 818 | 5 312 |
| scan: every entry in key order | 66 | 218 |
| commits: 2 000 one-put transactions | 5 689 | 10 913 |

Both bindings copy every value out and free it in the loop. Run it with
`cd bench && tur fetch && tur run src/bench/kv.tur`.

## Tests

`tur test tests` runs the suites: persistence, transactions (rollback on
`Err` and on panic, MVCC snapshots, nested transactions), limits (511-byte
keys, a 1 MiB map filled to `MapFull`, growing past it), errors, named and
duplicate-sorted databases, cursors, binary values, backup, bulk load, and
two reader threads beside a writer thread. `errors/run.sh` asserts the
linear-handle compile errors.

## Status

Needs a `tur` newer than v0.71.0: the scoped transactions rely on compiler
fixes (`^borrow` in a callback type, `?` inside callbacks) from turmeric's
`lmdb-spice` branch. With v0.71.0 the `txn`, `dbi`, `ops`, `cursor` and
`bytes` modules do not compile.

CI builds and tests it on Linux x86-64 and macOS arm64, and the benchmark
was measured on Linux x86-64. Linux arm64 has no CI leg here and is
untested. Windows (MSYS2 UCRT64) is unverified: lmdb-spice-plan section 7's
hand-run assessment has not been done yet.
