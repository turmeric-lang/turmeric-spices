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

### Request helpers -- `tourist/request`

Typed forwards to stdlib httpd, so a route never reaches the connection.
Strings they return belong to the request (valid until the response is sent):

| Helper | Returns |
|---|---|
| `(req-cookie ctx name)` | `(Option cstr)` -- a cookie sent empty is `some ""` |
| `(set-cookie! ctx opts)` | queues `Set-Cookie` from stdlib's `CookieOpts` (`cookie`, `cookie-full`) |
| `(form-param ctx key)` | `(Result cstr cstr)` -- decoded; `err` if the body is not a URL-encoded form or lacks the field |
| `(req-file ctx field)` | `(Option Part)`; `part-name`, `part-filename : (Option cstr)`, `part-content-type`, `part-data`, `part-data-len` |
| `(req-ip ctx)` / `(req-proto ctx)` / `(req-id ctx)` | client address, `"http"`/`"https"`, request id -- honouring `mw-trust-proxy` / `mw-request-id` in `tourist-with`'s stack |
| `(basic-auth realm verify)` | a `use!`-shaped check: 401 + challenge unless `verify` accepts; the route reads `(auth-user ctx)` |
| `(body-limit n)` | a `use!`-shaped check: 413 past `n` bytes |
| `(guarded check handler)` | runs such a check in front of one route |

```turmeric
(tourist 3000
  (get! "/" (fn [ctx : Ctx] : Response
              (let [t (req-cookie ctx "theme")]
                (text (if (some? t) (unwrap t) "light")))))
  (get! "/admin" (guarded (basic-auth "staff" check-password) admin-page))
  (post! "/upload" (guarded (body-limit 1048576) upload)))
```

```sweet-exp
tourist 3000
  get! "/" (fn [ctx : Ctx] : Response
             (let [t req-cookie(ctx "theme")]
               text(if(some?(t) unwrap(t) "light"))))
  get! "/admin" guarded(basic-auth("staff" check-password) admin-page)
  post! "/upload" guarded(body-limit(1048576) upload)
```

`serve-static!` serves files through stdlib's static serving: an `ETag` on
every file, `304 Not Modified` for a matching `If-None-Match`, binary-safe
bodies, and dotfiles (`.env`, `..`) refused.

### Server-level middleware -- stdlib httpd's set, via `tourist-with`

Express apps get most cross-cutting behaviour from connect middleware
(`app.use(cors())`). tourist's equivalent is stdlib httpd's middleware set,
run around the whole app: give `tourist-with` a `TouristOpts` whose wrap puts
a `compose-middleware` stack around tourist's dispatch. It sees every request
-- also the ones no route matches and the ones it answers itself (a CORS
preflight, a 429) -- and runs after tourist's response is written, so it can
decorate it.

```turmeric
(defn app-stack [inner : ptr<void>] : ptr<void>
  (compose-middleware inner
                      (mw-trust-proxy (ok-val (trust-proxy-opts "loopback")))
                      mw-log
                      mw-recover
                      (mw-cors-opts cors)
                      (mw-rate-limit (make-struct RateLimitOpts 100 60))))

(tourist-with 3000
  (tourist-opts (fn [inner : ptr<void>] : ptr<void> (app-stack inner)))
  (get! "/" (fn [_] (text "ok"))))
```

```sweet-exp
defn app-stack [inner : ptr<void>] : ptr<void>
  compose-middleware inner
                     mw-trust-proxy(ok-val(trust-proxy-opts("loopback")))
                     mw-log
                     mw-recover
                     mw-cors-opts(cors)
                     mw-rate-limit(make-struct(RateLimitOpts 100 60))

tourist-with 3000
  tourist-opts (fn [inner : ptr<void>] : ptr<void> (app-stack inner))
  get! "/" (fn [_] (text "ok"))
```

The leftmost layer is outermost. Put `mw-trust-proxy` outside what reads the
client address, and `mw-log` outside `mw-recover` so the 500 it makes is
logged (a panic skips the post-processing of every layer it unwinds through).
`TouristOpts` also carries httpd's `ServerOpts` (workers, body cap, read
timeout); `(default-tourist-opts)` is `tourist`'s own settings with no stack.
The wrap is called once when the server starts: build a fresh `TouristOpts`
for each server. `tourist-conn-with` is the same for apps with WebSocket
routes. A route that panics under `mw-recover` is answered 500 and leaks
nothing.

| Express | tourist |
|---|---|
| `cors` | `mw-cors-opts` (origin allow-list, credentials) |
| `morgan` | `mw-log`, or `mw-log-to` for your own sink |
| error-handling middleware | `mw-recover` (real panic recovery) |
| `express-rate-limit` | `mw-rate-limit` |
| `helmet` | `mw-secure-headers` / `mw-secure-headers-opts` |
| `express-request-id` | `mw-request-id`, read with `httpd-req-id` |
| `app.set('trust proxy', ...)` | `mw-trust-proxy`; `httpd-req-ip` / `httpd-req-proto` |
| `etag` (dynamic responses) | `mw-etag` |
| `connect-timeout` | `mw-timeout` (the handler still runs to completion) |
| `compression` | `mw-compress` (`stdlib/httpd-compress.tur`, needs the zlib spice) |
| basic auth | `mw-basic-auth` app-wide, or `basic-auth` + `guarded` per route |
| body-parser `limit` | `mw-body-size`, `ServerOpts`' body cap, or `body-limit` per route |
| body-parser `urlencoded` | `form-param` |
| `cookie-parser` / `res.cookie` | `req-cookie` / `set-cookie!` |
| `multer` | `req-file` |
| `req.ip` / `req.protocol` | `req-ip` / `req-proto` |
| `serve-static` | `serve-static!` (ETag + 304) |
| `app.use(fn)` | `use!` / `use-around!` / `use-after!` (above) |

The stdlib middleware are documented in the turmeric repo's
[httpd middleware guide](https://turmeric-lang.com/docs/html/guides/httpd-middleware-guide.html).

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
queued headers on a short-circuit. `tests/wrap_test.tur` runs an app under
`tourist-with` with trust-proxy, log, recover, CORS and rate-limit layers: a
panicking route answered 500 (and logged, and leak-free), a preflight answered
without reaching tourist, the client address in the log, a 429.
`tests/opts_test.tur` checks `ServerOpts` without a stack.
`tests/request_test.tur` covers every request helper and `serve-static!`'s
ETag / 304 / binary / dotfile behaviour. Run them with `tur test tests`.

`errors/run.sh` asserts the compile-fail fixtures: that Captures, Ctx, Pattern
and Item are not interchangeable even though all four are `:int` underneath.

Everything under `fixtures/` is a whole app needing a live listener, so those
stay manual -- see the per-fixture files.
