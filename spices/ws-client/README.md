# tur-ws-client

RFC 6455 WebSocket **client** for Turmeric: connect to any `ws://` or `wss://`
endpoint and exchange text and binary frames over a small synchronous API --
`ws-connect`, `ws-send`, `ws-recv`, `ws-close`.

## Overview

`tur-ws-client` implements the client half of RFC 6455. Plain `ws://`
connections go over raw POSIX sockets and have **no native dependencies** --
the handshake's SHA-1 and base64 are implemented inline. `wss://` is layered on
the bundled **mbedTLS 3.6.2** (pulled in via `tur fetch`) and is gated at
compile time on `__has_include(<mbedtls/ssl.h>)`: if mbedTLS is not on the
include path the spice still builds and `ws://` works, while a `wss://` connect
returns a null handle whose `ws-last-error` explains how to enable TLS. This is
the same optional-TLS pattern `tur-http` uses.

The connection handle `WsConn`, the frame view `WsFrame`, the frame predicates,
and `ws-accept-key` come from the shared [`ws-core`](../ws-core) spice
(NAME-V0), so `ws-client` and `ws-server` speak one set of types -- import
those names from `ws-core`. `ws-client` exports only the client-side behaviour
(`ws-connect` and friends).

It is a blocking, single-threaded client -- ideal for chat bots, market-data
feeds, dev-tools bridges, and echo tests. See *Non-goals* below for what it
deliberately leaves out.

### TLS certificate verification

`wss://` connections **verify the server certificate by default** (TLS-V0).
There are three entry points, so the trust policy is fixed at the call site and
an unsafe configuration cannot leak in by accident:

```turmeric no-check
;; Default: verify against the system CA store + check the hostname.
(ws-connect "wss://api.example.com/socket")

;; Verify against a private/self-signed CA bundle (e.g. an internal dev CA).
(ws-connect-with-ca "wss://localhost:8443/socket" "/path/to/dev-ca.pem")

;; Skip verification entirely. UNSAFE -- development against self-signed
;; servers only; never ship this.
(ws-connect-insecure "wss://localhost:8443/socket")
```

The system CA store is discovered at `/etc/ssl/certs/ca-certificates.crt`
(Debian/Ubuntu/Alpine), falling back to `/etc/pki/tls/certs/ca-bundle.crt`
(Fedora/RHEL). When no bundle is found, `ws-connect` fails with a message
pointing at `ws-connect-with-ca`. Verification failures (untrusted, expired, or
hostname-mismatched certs) return a null handle; read `ws-last-error` for the
reason.

## Install

```turmeric no-check
:spices {
  "ws-client" {:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "ws-client-v0.1.0"
               :subdir "spices/ws-client"}
}
```

Within this workspace it resolves as a sibling automatically; no fetch needed
for `ws://`. For `wss://`, run `tur fetch` once to build mbedTLS.

## Quick start

```turmeric
(import ws-client/client
  :refer [ws-connect ws-conn-null? ws-last-error
          ws-send ws-recv ws-frame-text ws-text? ws-close ws-free])

(let [c (ws-connect "ws://localhost:9000/feed")]
  (if (ws-conn-null? c)
    (println (ws-last-error))
    (do
      (ws-send c "hello")
      (let [f (ws-recv c)]
        (when (ws-text? f)
          (println (ws-frame-text f))))   ;; payload valid until next ws-recv
      (ws-close c)
      (ws-free c))))
```

```sweet-exp
#lang sweet-exp
(import ws-client/client
  :refer [ws-connect ws-conn-null? ws-last-error
          ws-send ws-recv ws-frame-text ws-text? ws-close ws-free])

let [c ws-connect("ws://localhost:9000/feed")]
  if ws-conn-null?(c)
    println(ws-last-error())
    do
      ws-send(c "hello")
      let [f ws-recv(c)]
        when ws-text?(f)
          println $ ws-frame-text f   ;; payload valid until next ws-recv
      ws-close(c)
      ws-free(c)
```

## API

| Function | Signature | Purpose |
|---|---|---|
| `ws-connect` | `(cstr) -> WsConn` | Open a `ws://`/`wss://` connection; `wss://` verifies against the system CA store. Null handle on failure. |
| `ws-connect-with-ca` | `(cstr cstr) -> WsConn` | Like `ws-connect`, verifying `wss://` against a custom CA bundle file. |
| `ws-connect-insecure` | `(cstr) -> WsConn` | Like `ws-connect` but skips `wss://` certificate verification (**unsafe**, dev only). |
| `ws-conn-null?` | `(WsConn) -> bool` | Test the failure sentinel from `ws-connect`. |
| `ws-last-error` | `() -> cstr` | Reason for the most recent failure (static storage). |
| `ws-send` | `(WsConn cstr) -> int` | Send a UTF-8 text frame (masked). |
| `ws-send-bytes` | `(WsConn ptr<void> int) -> int` | Send a binary frame (masked). |
| `ws-recv` | `(WsConn) -> WsFrame` | Receive the next (reassembled) message. |
| `ws-close` | `(WsConn) -> void` | Closing handshake, then close the socket. |
| `ws-free` | `(WsConn) -> void` | Release all resources (fd + memory + TLS). |
| `ws-set-timeout` | `(WsConn int) -> void` | Set receive timeout in ms (0 = block forever). |

`WsConn` is defined in [`ws-core`](../ws-core); `ws-accept-key` and the frame
accessors / predicates below also live there (NAME-V0) -- import them from
`ws-core`.

### Reading a frame

`ws-recv` returns an opaque `WsFrame` handle (from `ws-core/frame`) -- a **view
into the connection's internal reassembly buffer**, valid only until the next
`ws-recv` or `ws-free`. Copy the payload if you need it longer.

| Accessor (`ws-core/frame`) | Returns |
|---|---|
| `ws-frame-kind` | `int` -- `1` text, `2` binary, `8` close, `9` ping, `10` pong, `-1` timeout, `-2` error |
| `ws-frame-data` | `ptr<void>` -- payload pointer |
| `ws-frame-len` | `int` -- payload length |
| `ws-frame-text` | `cstr` -- payload as a NUL-terminated string |

Predicates (`ws-core/frame`): `ws-text?`, `ws-binary?`, `ws-ping?`, `ws-pong?`,
`ws-closed?`, `ws-timeout?`, `ws-error?`.

`ws-recv` reassembles fragmented messages, answers Ping frames with a Pong
automatically (then surfaces the Ping), and surfaces Pong and Close frames.

## Receive timeouts

```turmeric
(ws-set-timeout c 500)            ;; 500 ms
(let [f (ws-recv c)]
  (if (ws-timeout? f)
    (println "no message within 500ms")
    (handle f)))
```

```sweet-exp
ws-set-timeout(c 500)            ;; 500 ms
let [f ws-recv(c)]
  if ws-timeout?(f)
    println("no message within 500ms")
    handle(f)
```

`ws-set-timeout` sets `SO_RCVTIMEO`; a fired timeout yields a `:timeout` frame
rather than blocking forever. Pass `0` to disable.

## Non-goals (v0)

No async/reactor integration (blocking I/O only), no per-message deflate
(RFC 7692), no HTTP `CONNECT` proxy tunnelling, no subprotocol negotiation, and
sends are single frames (the client does not fragment outbound messages).

## Tests

- `tests/codec_test.tur` -- flat unit test (run by CI via `tur test tests`):
  asserts `ws-accept-key` against the RFC 6455 §4.2.2 vector, exercising the
  inline SHA-1 + base64 with no network.
- `fixtures/ws-echo-text/run.sh` -- live `ws://` round-trip against a
  pure-Python echo server (text frames + timeout path).
- `fixtures/ws-echo-tls/run.sh` -- live `wss://` round-trip over real TLS
  with a self-signed cert (requires `tur fetch` for mbedTLS).

## See also

- [WebSocket client guide](https://github.com/turmeric-lang/turmeric-spices/blob/main/docs/guides/websocket-client-guide.md)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/ws-client>
