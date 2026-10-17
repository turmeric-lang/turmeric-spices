# tur-httpd

A typed HTTP/1.1 server layer for Turmeric, over stdlib `httpd`.
Server-side counterpart to [`tur-http`](../http/).

## Overview

`tur-httpd` exposes a small synchronous request/response API: start a server
with a handler function, inspect the request, build a response, return. The
server underneath -- listener, worker pool, HTTP/1.1 keep-alive, read timeout,
body cap, TLS hook, connection upgrade -- is stdlib `httpd`
(`stdlib/httpd.tur`); this spice is the typed layer over it, the way Node's
`req`/`res` objects sit over its socket: `Request` is a view of the request
stdlib parsed, `Response` is a value the layer writes back, and the
`Handler` typeclass and JSON codecs build on those.

It is one of the spices that together form a composable web stack for
Turmeric:

| Spice          | Analogue            | Depends on                       |
|----------------|---------------------|----------------------------------|
| `tur-template` | ERB / EJS           | (none -- pure Turmeric)          |
| `tur-httpd`    | Node's `http` req/res | stdlib `httpd`                 |
| `tur-tourist`  | Express / scotty    | `tur-httpd`, `tur-template`      |

The layers are deliberately separate so any one can be used independently --
and stdlib `httpd` itself is a supported base, with its own middleware set.

## Install

```turmeric no-check
:spices {
  "httpd" {:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "httpd-v0.1.0"
           :subdir "spices/httpd"}
}
```

## Quick start

```turmeric
(import httpd/server   :refer [server-start server-stop])
(import httpd/response :refer [resp-ok])

(def srv (server-start 8080 (fn [req]
  (resp-ok "text/plain" "Hello, world!"))))

;; ... application code ...

(server-stop srv)
```

```sweet-exp
import httpd/server   :refer [server-start server-stop]
import httpd/response :refer [resp-ok]

def srv server-start(8080 (fn [req] (resp-ok "text/plain" "Hello, world!")))

;; ... application code ...

server-stop(srv)
```

## Server behaviour and options

These come from stdlib `httpd` (since tur-httpd 0.3.0; the spice's own server
before that had none of the limits):

- **Keep-alive.** HTTP/1.1 connections stay open for the next request. A
  client that reads to EOF to find the end of a response must send
  `Connection: close` (or use `Content-Length`). A `Connection: close` header
  on a `Response` closes the connection after it.
- **Loopback by default.** Servers bind `127.0.0.1` unless the program calls
  stdlib's `httpd-set-bind-any!` / `httpd-set-bind-addr!` (or sets
  `TUR_HTTPD_BIND_ANY=1`) before starting them.
- **Limits.** A request head must arrive within the read timeout (5 s; 408
  past it, 431 for a head over 256 KiB), and a body over the cap (8 MiB) is
  refused 413 before it is read. `server-start-opts` sets both per server
  (example below).
- **Binary bodies and the peer.** `req-body-len` gives the real length of a
  body that holds NULs, `with-body-bytes` sends one, and `req-remote-addr`
  returns the client's address as `(Option cstr)`. `with-header-set` replaces
  a header case-insensitively and `resp-header` reads one back.
- **stdlib accessors.** `(request-conn req)` is the stdlib conn behind a
  `Request`, for the stdlib accessors this layer does not wrap (cookies,
  forms, multipart).

`ServerOpts` holds the pool size (`server-start` uses 8), the body cap in
bytes (-1 keeps stdlib's) and the read timeout in ms (0 keeps stdlib's):

```turmeric
(import httpd/server :refer [ServerOpts server-start-opts server-port server-stop])

(let [srv (server-start-opts 0 my-handler (make-struct ServerOpts 8 1048576 2000))]
  (println (server-port srv))     ; kernel-assigned port
  (server-stop srv))
```

```sweet-exp
import httpd/server :refer [ServerOpts server-start-opts server-port server-stop]

let [srv server-start-opts(0 my-handler make-struct(ServerOpts 8 1048576 2000))]
  println $ server-port srv
  server-stop srv
```

## Stdlib middleware around a typed handler

`server-start-wrapped` (and `server-start-conn-wrapped` for two-arg
handlers) runs the handler inside a stack of stdlib httpd middleware --
`mw-recover`, `mw-cors-opts`, `mw-log`, `mw-rate-limit`, `mw-secure-headers`,
`mw-request-id`, `mw-trust-proxy`, `mw-etag`, ... -- given as a `server-wrap`
function from the stdlib handler this spice builds to the one to serve:

```turmeric
(server-start-wrapped 8080 my-handler (default-server-opts)
  (server-wrap (fn [inner : ptr<void>] : ptr<void>
                 (compose-middleware inner mw-log mw-recover mw-secure-headers))))
```

```sweet-exp
server-start-wrapped 8080 my-handler default-server-opts()
  server-wrap (fn [inner : ptr<void>] : ptr<void>
                (compose-middleware inner mw-log mw-recover mw-secure-headers))
```

The handlers are stdlib's own representation, the `ptr<void>` every `mw-*`
takes and returns. A `ServerWrap` is called once when the server starts and
released then, so build one per server. Middleware that answers by itself (a
preflight, a 429) never reaches the typed handler; middleware after `next`
sees the Response already on the conn. A handler that panics under
`mw-recover` is answered 500, and its Request view is still freed. tourist's
`tourist-with` is built on this.

A typed handler can also let one stdlib function answer and hand the result
back as its Response: `take-conn-response` moves what the stdlib conn holds
(status, headers, body) into a new Response. tourist's `serve-static!` serves
files that way, through stdlib's `httpd-static-serve!`.

## Typed handlers (`Handler` typeclass)

`server-start` takes a bare `(c-fn [int] int)` whose `int`s are really a
`Request` and a `Response` with the types erased. The `httpd/handler` module
lifts that contract to the type layer: a handler is a witness type with a
`Handler` instance whose `respond` method maps a `Request` to a `Response`
directly. `serve` generates the captureless trampoline and starts the server.

```turmeric
(import httpd/handler  :refer [serve])
(import httpd/request  :refer [req-path])
(import httpd/response :refer [resp-ok not-found])
(import httpd/server   :refer [server-stop])

;; The witness is a defopaque :int used only to select the instance; its
;; value carries no state (the method reads everything from the Request).
(defopaque App :int)

(definstance Handler [App]
  (respond [self req]
    (if (cstr-eq? "/health" (req-path (:: req :Request)))
      (resp-ok "text/plain" "ok")
      (not-found "no such route"))))

(def srv (serve 8080 App))     ;; serve-pool / serve-spawn mirror server-start-*
;; ... application code ...
(server-stop srv)
```

The method is named `respond` (not `handle`, which is a reserved special
form). `server-start` and the bare `(c-fn [int] int)` API remain available as
the lower-level path.

## JSON body codecs

`httpd/handler` also exposes helpers that decode a typed request body and
encode a typed response body through the json spice's `EncodeJson`/`DecodeJson`
typeclasses, so a JSON endpoint needs no inline-C body parsing:

- `(with-json-body req T f)` -- decode the body into `T`; on success call
  `f` with the decoded value (which returns a `Response`), otherwise reply
  `400 Bad Request` with the decode error.
- `(req-decode req T) : (Result T cstr)` -- the lower-level decode used by
  `with-json-body`.
- `(json-ok x) : Response` / `(json-resp status x) : Response` -- encode `x`
  as the JSON body of a `200` (or arbitrary-status) response, with
  `Content-Type: application/json`.

```turmeric
(defstruct EchoReq  [msg : cstr])
(defstruct EchoResp [echo : cstr  len : int])
(derive-json EchoReq  (msg cstr))
(derive-json EchoResp (echo cstr) (len int))

(defopaque Echo :int)
(definstance Handler [Echo]
  (respond [self req]
    (with-json-body (:: req :Request) EchoReq
      (fn [r : EchoReq]
        (json-ok (make-struct EchoResp (.msg r) (str-len (.msg r))))))))
```

These build on the json codec surface. `httpd` does not declare `yyjson` in its
own `:cmake-deps`: the compiler collects `:cmake-deps` transitively across the
declared `:spices` (and workspace siblings), so `yyjson` is inherited from the
`json` dependency. (Likewise `mbedTLS` is inherited from the optional `tls`
dependency.)

## TLS (HTTPS / `wss://`)

The optional `httpd/tls` module adds a TLS-terminating listener,
`server-start-tls-conn`, the encrypted counterpart of `server-start-conn`. It
reuses the [`tur-tls`](../tls) spice for the cert/key + handshake and hands each
two-arg handler a `Conn` whose `conn-tls` slot carries the per-connection TLS
state -- which is exactly what `ws-server`'s `ws-upgrade` consumes to serve
`wss://`.

```turmeric no-check
(import httpd/tls :refer [server-start-tls-conn server-stop-tls])

(let [srv (server-start-tls-conn 8443 "cert.pem" "key.pem" handler)]
  ;; ... handler is (fn [conn : Conn req : Request] : Response) ...
  (server-stop-tls srv))
```

TLS is opt-in: only programs that import `httpd/tls` link mbedTLS. The listener
is stdlib `httpd-new-tls`; `httpd/tls` registers the tls spice's functions in
stdlib's TLS hook table (`tls-httpd-init`) for you.

## Connection upgrade

A two-arg handler's `conn-mark-upgraded!` is stdlib's `httpd-conn-detach!`:
from that call the connection -- fd and TLS state -- belongs to the handler,
the server writes nothing after it returns and closes nothing. ws-server's
`ws-upgrade` is built on it. An upgraded session holds its worker for as long
as it runs, so size the pool for the sessions you expect.

## Status

Requires turmeric v0.64.1 or later (`:tur-version ">=0.64.1"`), the first
release with stdlib httpd's connection upgrade and per-server limits
([tourist-on-stdlib-httpd-plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/tourist-on-stdlib-httpd-plan.md)
H1). See
[`docs/archive/history/tur-httpd-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/history/tur-httpd-plan.md) in the
turmeric repo for the full roadmap.

## See also

- [HTTP server guide](https://turmeric-lang.com/docs/html/guides/httpd-guide.html)
- [Async guide](https://turmeric-lang.com/docs/html/guides/httpd-async-guide.html)
- [Middleware guide](https://turmeric-lang.com/docs/html/guides/httpd-middleware-guide.html)
- [API reference](api/) (generated)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/httpd>
