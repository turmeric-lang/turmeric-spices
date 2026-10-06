# tur-tourist

Sinatra/scotty-style HTTP micro-framework for Turmeric, layered on top of
`tur-httpd`.

## Overview

`tur-tourist` provides routing, response helpers, middleware, and static file
serving. It follows the idiom from Haskell's
[scotty](https://hackage.haskell.org/package/scotty): a small DSL for
declaring routes and writing handlers.

It is one of three spices that together form a composable web stack for
Turmeric:

| Spice | Analogue | Depends on |
|---|---|---|
| `tur-template` | ERB / EJS | (none -- pure Turmeric) |
| `tur-httpd` | Mongoose / Civetweb | (none -- POSIX sockets + pthreads) |
| `tur-tourist` | Haskell's scotty | `tur-httpd`, `tur-template` |

The three are deliberately separate so any layer can be used independently.

## Install

```turmeric no-check
:spices {
  "tourist" {:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "tourist-v0.1.0"
             :subdir "spices/tourist"}
}
```

## Quick start

```turmeric
(import tourist/app    :refer [tourist])
(import tourist/dsl    :refer [get! post!])
(import tourist/param  :refer [capture])
(import tourist/helpers :refer [text])

(tourist 3000
  (get! "/hello/:name"
    (fn [req]
      (text (str-concat "Hello, " (ok-val (capture req "name")) "!"))))
  (post! "/echo"
    (fn [req]
      (text (req-body req)))))
```

## Status

Early in development. See
[`docs/archive/history/tur-tourist-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/history/tur-tourist-plan.md) in the
turmeric repo for the full roadmap.

## See also

- [Routing guide](https://turmeric-lang.com/docs/html/guides/tourist-routing-guide.html)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/tourist>

## Tests

`tests/router_test.tur` covers `tourist/router` -- pattern compilation and
matching -- which is the part of this spice that runs without a socket:
literal segments, named captures (single, multiple, and the empty-segment and
extra-segment misses), the greedy `*` wildcard, and trailing-slash tolerance.
Run it with `tur test tests`.

`errors/run.sh` asserts the compile-fail fixtures: that Captures, Ctx, Pattern
and Item are not interchangeable even though all four are `:int` underneath.

Everything under `fixtures/` is a whole app needing a live listener, so those
stay manual -- see the per-fixture files.
