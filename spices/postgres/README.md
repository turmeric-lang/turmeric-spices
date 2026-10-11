# tur-postgres

PostgreSQL client for Turmeric via libpq: connect, exec, parameterized
queries, prepared statements, transactions, and `LISTEN`/`NOTIFY`.

## Overview

`tur-postgres` is a `cmake-dep` spice that wraps libpq. It exposes the
familiar PostgreSQL workflow: open a connection with `db-connect`, run
queries with `db-query` / `db-query-params`, prepare statements via
`stmt-prepare`, and walk rows with `row-get` / `col-name`.

It also surfaces `LISTEN` / `NOTIFY` so you can build event-driven services
that react to row changes without polling.

## Install

```turmeric no-check
:spices {
  "postgres" {:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "postgres-v0.1.0"
              :subdir "spices/postgres"}
}
```

libpq itself comes from the system -- there is no source build to fall back
on, so `tur fetch` stops with a message naming it when it is missing:

- Debian/Ubuntu: `apt-get install libpq-dev`
- macOS: `brew install libpq`. Homebrew keeps it keg-only; the spice already
  points CMake at `/opt/homebrew/opt/libpq`, so on an Intel Mac or a custom
  Homebrew prefix also `export PostgreSQL_ROOT="$(brew --prefix libpq)"`.

## Quick start

```turmeric
(import postgres/db  :refer [db-connect db-close db-query])
(import postgres/row :refer [row-get rows-free])

(let [r (db-connect "postgresql://localhost/app")]
  (when (ok? r)
    (let [db   (ok-val r)
          rows (ok-val (db-query db "SELECT id, name FROM users"))]
      (for [row rows]
        (println (row-get row "id") (row-get row "name")))
      (rows-free rows)
      (db-close db))))
```

```sweet-exp
#lang sweet-exp
import postgres/db  :refer [db-connect db-close db-query]
import postgres/row :refer [row-get rows-free]

let [r db-connect("postgresql://localhost/app")]
  when ok?(r)
    let [db   ok-val(r)
         rows ok-val(db-query(db "SELECT id, name FROM users"))]
      for [row rows]
        println(row-get(row "id") row-get(row "name"))
      rows-free(rows)
      db-close(db)
```

### Linear `Conn` / `Rows` (U1)

`Conn` and `Rows` are `:linear` opaques. A connection from `ok-val` on
`db-connect` must be closed exactly once with `db-close` (PQfinish), and
a result set from `ok-val` on `db-query` / `db-query-params` /
`stmt-exec-prepared` must be released exactly once with `rows-free`
(PQclear). The exec / query / transaction / prepared-statement / notify
operations and the row accessors take their handle by `^borrow`. Under
`-Xsubstructural` this makes use-after-close, use-after-free, double-close,
and leaked connections / result sets into compile-time errors (`TUR-E0101`
/ `TUR-E0100`) instead of runtime faults. The discipline is inert in
ordinary builds, so existing call sites compile unchanged. (Prepared
statements are referenced by name, so there is no statement handle to
track.) See `errors/` for the rejected cases.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/postgres>
