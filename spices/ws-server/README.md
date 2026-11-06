# tur-ws-server

RFC 6455 WebSocket **server** upgrade for [`tur-httpd`](../httpd): turn an
ordinary HTTP handler into a WebSocket endpoint with a single call to
`ws-upgrade`, then exchange text and binary frames over a small synchronous
API -- `ws-server-send`, `ws-server-recv`, `ws-server-close`.

## Overview

A WebSocket connection starts life as an HTTP/1.1 request carrying
`Upgrade: websocket`. `tur-ws-server` plugs into httpd's **two-arg handler**
shape (`server-start-conn` / `serve-conn`): the handler receives a `Conn`
alongside the `Request`, and `ws-upgrade` validates the handshake, writes
`101 Switching Protocols`, takes ownership of the socket, and runs your
WebSocket handler for the lifetime of the session.

It reuses httpd's existing worker-thread model -- no new thread infrastructure.
Plain `ws://` goes over raw POSIX sockets with **no native dependencies** (the
handshake's SHA-1 and base64 are implemented inline). Server-to-client frames
are sent **unmasked** (RFC 6455 §5.1); client-to-server frames arrive masked
and are unmasked on read.

## Install

```turmeric no-check
:spices {
  "ws-server" {:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "ws-server-v0.1.0"
               :subdir "spices/ws-server"}
}
```

Within this workspace it resolves as a sibling automatically; it depends on the
sibling `httpd` spice (for the `Conn` handle and the `server-start-conn` entry
point) and on [`ws-core`](../ws-core) for the shared `WsConn` / `WsFrame` types
and frame predicates (NAME-V0). Import those type-level names from `ws-core`;
`ws-server` itself exports only the server-side behaviour (`ws-upgrade`,
`ws-server-send`, ...).

## Quick start

A mixed REST + WebSocket server: `/health` answers over HTTP, `/ws` echoes.

```turmeric
(import httpd/types    :refer [Conn Request])
(import httpd/server   :refer [server-start-conn server-stop])
(import httpd/request  :refer [req-method req-path])
(import httpd/response :refer [resp-ok not-found bad-request])
(import ws-core/conn   :refer [WsConn])
(import ws-core/frame  :refer [ws-frame-text ws-text? ws-closed?])
(import ws-server/server
  :refer [ws-upgrade ws-server-recv ws-server-send ws-server-close])

(defn echo-loop [ws : WsConn] : void
  (let [f (ws-server-recv ws)]
    (if (ws-closed? f)
      (ws-server-close ws)
      (do (when (ws-text? f) (ws-server-send ws (ws-frame-text f)))
          (echo-loop ws)))))

(defn handler [conn : Conn req : Request] : Response
  (cond
    (and (= (req-method req) "GET") (= (req-path req) "/health"))
    (resp-ok "text/plain" "ok")

    (and (= (req-method req) "GET") (= (req-path req) "/ws"))
    (if (ok? (ws-upgrade conn req echo-loop))
      (resp-ok "text/plain" "")              ;; placeholder; worker skips it
      (bad-request "not a websocket request"))

    :else (not-found "")))

(defn main [] : int
  (let [srv (server-start-conn 8080 handler)]
    ;; ... wait / signal ...
    (server-stop srv)
    0))
```

```sweet-exp
#lang sweet-exp
(import httpd/types    :refer [Conn Request])
(import httpd/server   :refer [server-start-conn server-stop])
(import httpd/request  :refer [req-method req-path])
(import httpd/response :refer [resp-ok not-found bad-request])
(import ws-core/conn   :refer [WsConn])
(import ws-core/frame  :refer [ws-frame-text ws-text? ws-closed?])
(import ws-server/server
  :refer [ws-upgrade ws-server-recv ws-server-send ws-server-close])

defn echo-loop [ws : WsConn] : void
  let [f ws-server-recv(ws)]
    if ws-closed?(f)
      ws-server-close(ws)
      do
        when ws-text?(f) ws-server-send(ws ws-frame-text(f))
        echo-loop(ws)

defn handler [conn : Conn req : Request] : Response
  (cond
    {{req-method(req) = "GET"} and {req-path(req) = "/health"}}
    resp-ok("text/plain" "ok")

    {{req-method(req) = "GET"} and {req-path(req) = "/ws"}}
    (if ok?(ws-upgrade(conn req echo-loop))
      resp-ok("text/plain" "")              ;; placeholder; worker skips it
      bad-request("not a websocket request"))

    :else not-found(""))

defn main [] : int
  let [srv server-start-conn(8080 handler)]
    ;; ... wait / signal ...
    server-stop(srv)
    0
```

## API

| Function | Signature | Purpose |
|---|---|---|
| `ws-upgrade` | `(Conn Request (fn [WsConn] void)) -> (Result bool UpgradeError)` | Validate + perform the upgrade, then run the handler. `ok(true)` on success, `err(UpgradeError)` otherwise. |
| `ws-upgrade-or-zero` | `(Conn Request (fn [WsConn] void)) -> int` | **Deprecated** (UPG-V0): the old `1`/`0` shape, for one-release migration. |
| `ws-server-send` | `(WsConn cstr) -> int` | Send a UTF-8 text frame (unmasked). |
| `ws-server-send-bytes` | `(WsConn ptr<void> int) -> int` | Send a binary frame (unmasked). |
| `ws-server-recv` | `(WsConn) -> WsFrame` | Receive the next (reassembled) message. |
| `ws-server-close` | `(WsConn) -> void` | Closing handshake, then close the socket. |
| `ws-set-server-timeout` | `(WsConn int) -> void` | Set receive timeout in ms (0 = block forever). |

### Upgrade result (UPG-V0)

`ws-upgrade` returns a `(Result bool UpgradeError)` rather than a bare int, so a
rejected handshake tells you *why*. `ok?` is true once the upgrade succeeded and
your handler ran -- in which case the `Response` your httpd handler returns
afterward is ignored (the connection has been hijacked and the worker skips its
normal serialize/write/close step). On failure, match `(err-val r)` on:

| `UpgradeError` variant | Meaning |
|---|---|
| `(UpgradeMissingHeader name)` | required header `name` (`"Upgrade"` / `"Sec-WebSocket-Key"`) absent |
| `(UpgradeBadVersion requested)` | `Sec-WebSocket-Version` present but not `13` |
| `(UpgradeUnsupportedProto offered)` | the `Upgrade` header did not name `websocket` |
| `(UpgradeWriteFailed)` | the `101` handshake could not be written |

```turmeric no-check
(let [r (ws-upgrade conn req my-handler)]
  (if (ok? r)
    (resp-ok "text/plain" "")
    (match (err-val r)
      (UpgradeMissingHeader name)    (bad-request name)
      (UpgradeBadVersion requested)  (bad-request "use Sec-WebSocket-Version: 13")
      (UpgradeUnsupportedProto kind) (bad-request "not a websocket upgrade")
      (UpgradeWriteFailed)           (bad-request "handshake write failed"))))
```

The handler argument is a `(fn [WsConn] void)`, called once the upgrade
succeeds. (The plan sketched a `^linear WsConn` handler that takes over closing
the socket; because `WsConn` is the shared, non-linear `ws-core` type used by
`ws-client` too, `ws-upgrade` instead retains ownership and frees the
connection after the handler returns -- a safe default. Migrating callers that
still want the old shape can use `ws-upgrade-or-zero` for one release.)

### Reading a frame

`ws-server-recv` returns an opaque `WsFrame` -- a **view into the connection's
reassembly buffer**, valid only until the next `ws-server-recv`. Copy the
payload if you need it longer.

`WsConn` / `WsFrame` and the accessors / predicates below come from
[`ws-core`](../ws-core) (NAME-V0) -- import them from there.

| Accessor (`ws-core/frame`) | Returns |
|---|---|
| `ws-frame-kind` | `int` -- `1` text, `2` binary, `8` close, `9` ping, `10` pong, `-1` timeout, `-2` error |
| `ws-frame-data` | `ptr<void>` -- payload pointer |
| `ws-frame-len` | `int` -- payload length |
| `ws-frame-text` | `cstr` -- payload as a NUL-terminated string |

Predicates (`ws-core/frame`): `ws-text?`, `ws-binary?`, `ws-ping?`, `ws-pong?`,
`ws-closed?`, `ws-timeout?`, `ws-error?`.

`ws-server-recv` reassembles fragmented messages and answers Ping frames with a
Pong automatically (then surfaces the Ping); Pong and Close frames are surfaced
to the caller.

## Using the `serve-conn` macro

If you select handlers by witness type (the `Handler` / `ConnHandler`
typeclass idiom), `serve-conn` bridges a `ConnHandler` instance to
`server-start-conn`:

```turmeric
(defopaque WsEcho :int)
(definstance ConnHandler [WsEcho]
  (respond-conn [self conn req]
    (if (ok? (ws-upgrade conn req echo-loop))
      (resp-ok "text/plain" "")
      (bad-request "not a websocket request"))))

(def srv (serve-conn 8080 WsEcho))         ;; top-level form
```

```sweet-exp
#lang sweet-exp
defopaque WsEcho :int
definstance ConnHandler [WsEcho]
  respond-conn [self conn req]
    if ok?(ws-upgrade(conn req echo-loop))
      resp-ok("text/plain" "")
      bad-request("not a websocket request")

def srv serve-conn(8080 WsEcho)         ;; top-level form
```

## `wss://` (TLS)

Secure WebSocket is supported (plan phase WS4) by terminating TLS in httpd
itself. Start the listener with httpd's TLS entry point instead of
`server-start-conn` and everything else is identical -- `ws-upgrade` reads the
per-connection TLS state off the `Conn` and runs the handshake and every frame
over the encrypted stream:

```turmeric no-check
(import httpd/tls :refer [server-start-tls-conn server-stop-tls])

(let [srv (server-start-tls-conn 8443 "cert.pem" "key.pem" handler)]
  ;; ... handler calls (ws-upgrade conn req ...) exactly as for ws:// ...
  (server-stop-tls srv))
```

TLS is gated at compile time on the mbedTLS headers (the same `__has_include`
pattern as `ws-client`), so when mbedTLS is absent the spice still builds and
`ws://` works; only `wss://` requires it. httpd terminates TLS via the
[`tur-tls`](../tls) spice; see its `tools/gen-cert.sh` for a throwaway dev cert.

## Using with tourist

Building on the [`tourist`](../tourist) framework? Use
[`tur-tourist-ws`](../tourist-ws), which wraps `ws-upgrade` in a `ws-route!`
that drops straight into a tourist app's route list -- no need to wire up
`server-start-conn` and call `ws-upgrade` by hand.

## Non-goals (v0)

- No broadcast / pub-sub hub built in -- it is a few lines of user code
  (`Mutex<vec<WsConn>>` + a loop); see `tests/broadcast_test.tur` for a
  worked example.
- No per-message deflate (RFC 7692), no subprotocol negotiation, no HTTP/2.
- Blocking receive only (no reactor-integrated async).

## Tests

- `tests/echo_test.tur` -- flat round-trip test (run by CI via
  `tur test tests`): starts a `server-start-conn` echo server and drives it
  with an inline raw-socket WebSocket client (masked client frames in, unmasked
  echoes out), covering the handshake, five text messages, and the 16-bit
  length path. It cannot use the `ws-client` spice in-process because both
  spices define `WsConn` / `WsFrame`.
- `fixtures/echo/run.sh` -- live round-trip with the **`ws-client`**
  spice as the driver, run as a separate process to avoid that type clash.
- `tests/broadcast_test.tur` -- flat broadcast fan-out test (run by CI via
  `tur test tests`): a `Mutex<vec<WsConn>>` hub built from plain user code on
  top of the spice surface. Three inline raw-socket clients connect, one sends
  a single text frame, and three reader threads each verify they receive it --
  exercising the fan-out to every registered connection under the hub lock.
- `fixtures/broadcast/run.sh` -- the same broadcast scenario driven by
  three concurrent **`ws-client`** connections in a separate process.

## See also

- [WebSocket server guide](https://github.com/turmeric-lang/turmeric-spices/blob/main/docs/guides/websocket-server-guide.md)
- [tur-httpd](../httpd) -- the HTTP/1.1 server this upgrades from
- [tur-ws-client](../ws-client) -- the client counterpart
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/ws-server>
