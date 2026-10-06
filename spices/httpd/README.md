# tur-httpd

Minimal threaded HTTP/1.1 server for Turmeric, built on POSIX sockets and
`pthreads`. Server-side counterpart to [`tur-http`](../http/).

## Overview

`tur-httpd` is a Tier 1 spice (no external `cmake-dep`). It exposes a small
synchronous request/response API: start a server with a handler function,
inspect the request, build a response, return.

It is one of three spices that together form a composable web stack for
Turmeric:

| Spice          | Analogue            | Depends on                       |
|----------------|---------------------|----------------------------------|
| `tur-template` | ERB / EJS           | (none -- pure Turmeric)          |
| `tur-httpd`    | Mongoose / Civetweb | (none -- POSIX sockets/pthreads) |
| `tur-turist`   | Haskell's scotty    | `tur-httpd`, `tur-template`      |

The three are deliberately separate so any layer can be used independently.

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

TLS is opt-in: only programs that import `httpd/tls` link mbedTLS. The plaintext
`server-start*` path stays dependency-free.

## Status

Early in development. See
[`docs/archive/history/tur-httpd-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/history/tur-httpd-plan.md) in the
turmeric repo for the full roadmap.

## See also

- [HTTP server guide](https://turmeric-lang.com/docs/html/guides/httpd-guide.html)
- [Async guide](https://turmeric-lang.com/docs/html/guides/httpd-async-guide.html)
- [Middleware guide](https://turmeric-lang.com/docs/html/guides/httpd-middleware-guide.html)
- [API reference](api/) (generated)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/httpd>
