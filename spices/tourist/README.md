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
| `tur-httpd` | Node's `http` req/res | stdlib `httpd` |
| `tur-tourist` | Express / Haskell's scotty | `tur-httpd`, `tur-template` |

The layers are deliberately separate so any one can be used independently.
Underneath it all is stdlib `httpd` -- the server, as `http` is under Express.

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

```sweet-exp
import tourist/app    :refer [tourist]
import tourist/dsl    :refer [get! post!]
import tourist/param  :refer [capture]
import tourist/helpers :refer [text]

tourist 3000
  get! "/hello/:name"
    (fn [req]
      (text (str-concat "Hello, " (ok-val (capture req "name")) "!")))
  post! "/echo"
    (fn [req]
      (text (req-body req)))
```

## Middleware

Three kinds, all of which may capture (the closure's environment survives the
dispatch boundary, under concurrent requests):

| Form | Shape | Runs |
|---|---|---|
| `use!` | `(fn [Ctx] (Option Response))` | before the route, in declaration order; `(some resp)` short-circuits |
| `use-around!` | `(fn [Ctx (fn [Ctx] Response)] Response)` | around everything inside it -- the Express/Koa `next` shape; the first declared is outermost |
| `use-after!` | `(fn [Ctx Response] Response)` | after the route and every `use-around!`, in reverse declaration order |

Headers queued with `ctx-add-header!` land on whatever response goes out, a
`use!` short-circuit included.

```turmeric
(defn timing [label : cstr] : (fn [Ctx (fn [Ctx] Response)] Response)
  (fn [ctx : Ctx next : (fn [Ctx] Response)] : Response
    (let [resp (next ctx)]
      (with-header resp "X-Served-By" label))))

(tourist 3000
  (use-around! (timing "app-1"))
  (get! "/" (fn [_] (text "ok"))))
```

```sweet-exp
defn timing [label : cstr] : (fn [Ctx (fn [Ctx] Response)] Response)
  (fn [ctx : Ctx next : (fn [Ctx] Response)] : Response
    (let [resp (next ctx)]
      (with-header resp "X-Served-By" label)))

tourist 3000
  use-around! timing("app-1")
  get! "/" (fn [_] (text "ok"))
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
`tests/middleware_test.tur` runs a live app: capturing `use!` / `use-around!` /
`use-after!` closures under eight concurrent clients, nesting order, and
queued headers on a short-circuit. Run them with `tur test tests`.

`errors/run.sh` asserts the compile-fail fixtures: that Captures, Ctx, Pattern
and Item are not interchangeable even though all four are `:int` underneath.

Everything under `fixtures/` is a whole app needing a live listener, so those
stay manual -- see the per-fixture files.
