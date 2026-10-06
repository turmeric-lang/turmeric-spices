# tur-nng

Scalability protocols for Turmeric via
[nng](https://github.com/nanomsg/nng) (nanomsg-next-generation) -- request/reply,
publish/subscribe, pipeline, pair, bus, and survey, over `inproc://`, `ipc://`,
and `tcp://`.

## Overview

`tur-nng` is a `cmake-dep` spice that builds nng from source, statically, with
TLS and the test/tool targets off. The surface is intentionally thin: nng runs
its own worker threads and poller, so this spice is a set of blocking calls over
one linear `Socket` handle, plus a length-prefixed `Payload` for binary
messages.

What you get over raw sockets is a **messaging pattern** rather than a byte
stream: nng frames messages, reconnects dropped peers in the background,
load-balances a pipeline across workers, and filters a subscription by topic --
none of which you write yourself.

The `inproc://` transport connects two sockets inside one process with no
network and no port, which is what makes this spice's whole test suite
network-free and CI-safe. It is also a real answer for in-process fan-out:
the same code moves to `tcp://` by changing a string.

## Install

```turmeric no-check
:spices {
  "nng" {:url    "https://github.com/turmeric-lang/turmeric-spices"
         :ref    "nng-v0.1.0"
         :subdir "spices/nng"}
}
```

Nothing to install on the host: `tur fetch` clones and builds nng itself.

## Quick start

A request/reply round trip over `inproc://`:

```turmeric
(import nng/socket :refer [req-open rep-open dial listen close
                           set-recv-timeout-ms])
(import nng/msg    :refer [send-str recv-str])

(let [rep (ok-val (rep-open))
      req (ok-val (req-open))]
  (listen rep "inproc://demo")
  (dial   req "inproc://demo")
  (set-recv-timeout-ms rep 2000)
  (set-recv-timeout-ms req 2000)

  (send-str req "ping")
  (let [asked (recv-str rep)]
    (when (ok? asked)
      (println (ok-val asked))       ;; "ping"
      (send-str rep "pong")))
  (let [heard (recv-str req)]
    (when (ok? heard)
      (println (ok-val heard))))     ;; "pong"

  (close req)
  (close rep))
```

```sweet-exp
#lang sweet-exp
import nng/socket :refer [push-open pull-open dial listen close set-recv-timeout-ms]
import nng/msg    :refer [send-str recv-str]

let [push ok-val(push-open())
     pull ok-val(pull-open())]
  listen(push "inproc://jobs")
  dial(pull "inproc://jobs")
  set-recv-timeout-ms(pull 2000)
  send-str(push "job:42")
  let [r recv-str(pull)]
    when ok?(r)
      println $ ok-val r
  close(pull)
  close(push)
```

## Picking a protocol

Each constructor opens a socket speaking one protocol. A socket only talks to a
peer speaking the matching half.

| Pattern | Constructors | Shape | Reach for it when |
| --- | --- | --- | --- |
| Request / reply | `req-open` / `rep-open` | one request -> exactly one reply, retried automatically | RPC; the caller needs an answer |
| Pipeline | `push-open` / `pull-open` | each message to exactly ONE peer, round-robin | a job queue; add workers to go faster |
| Publish / subscribe | `pub-open` / `sub-open` | each message to EVERY matching subscriber; no queueing for absent ones | live feeds, telemetry, notifications |
| Pair | `pair-open` | two peers, both directions, no turn-taking | a dedicated link between two components |
| Bus | `bus-open` | each message to every directly connected peer, one hop | small peer groups that all see everything |
| Survey | `surveyor-open` / `respondent-open` | one question broadcast, many answers, bounded by a deadline | service discovery, quorum polls |

The `Socket` type is the same for all ten. Sending on a receive-only socket, or
subscribing on a socket that is not a SUB socket, is a runtime `err` carrying
`NNG_ENOTSUP` -- not a compile error. Per-protocol types are a deliberate
non-goal for v0; see "Not in v0" below.

## Addresses

| Scheme | Example | Notes |
| --- | --- | --- |
| `inproc://` | `inproc://jobs` | same process, no network. The name is a bare label, not a path |
| `ipc://` | `ipc:///tmp/jobs.sock` | same machine, Unix domain socket (a named pipe on Windows) |
| `tcp://` | `tcp://0.0.0.0:5555` to listen, `tcp://host:5555` to dial | across machines |

`listen` binds and fails immediately if the address is taken; `dial` connects
and, once connected, reconnects in the background if the peer goes away.

## Timeouts

**Set a receive timeout on anything that might not get an answer.** Without one,
a receive on a quiet socket parks the calling thread forever:

```turmeric
(set-recv-timeout-ms s 100)
(let [r (recv-str s)]
  (if (ok? r)
    (handle (ok-val r))
    (if (timed-out? (err-val r))
      (nothing-yet)                    ;; not a failure -- just nothing to read
      (println (err-str (err-val r))))))
```

`timed-out?` is the one error code worth branching on; `err-str` renders any of
them via `nng_strerror`.

## Payloads: `cstr` or `Payload`

`send-str` / `recv-str` are the convenience path and carry text. The message
must not contain NUL bytes -- `recv-str` terminates the string it hands back, so
an interior NUL truncates it.

`send-payload` / `recv-payload` are the real path. `Payload` is a heap block
laid out as `{ int64 len; uint8 data[] }`, so binary messages cross intact:

```turmeric
(import nng/payload :refer [payload-of-cstr payload-free payload->hex])
(import nng/msg :refer [send-payload recv-payload])

(let [b (payload-of-cstr "job:42")]
  (send-payload s b)
  (payload-free b))                        ;; send BORROWS; the Payload is still yours

(let [r (recv-payload s)]
  (when (ok? r)
    (println (payload->hex (ok-val r)))
    (payload-free (ok-val r))))            ;; recv gives you a fresh one to free
```

Ownership never involves nng's allocator. A receive asks nng for the message,
copies it, and calls `nng_free` before returning -- so everything handed back is
an ordinary heap block you release the ordinary way.

### Why `Payload` and not `Buf`

The layout is deliberately identical to [`tur-msgpack`](../msgpack/)'s `Buf`
(and to stdlib `serial.tur`'s bytes value) -- the same `{ int64 len; uint8
data[] }` block from the same `malloc`. The **name** differs because an opaque
resolves globally: two spices that each define a `Buf` cannot both be loaded by
one program, and msgpack-over-nng is the pairing both spices' plans were written
for. `tur-json` 0.4.0 renamed its `Encode` / `Decode` classes for exactly this
reason, so the msgpack cross-check program could exist at all.

With the names distinct, crossing between them is a copy against a documented
layout rather than a reinterpret -- see `tests/nng/msgpack_test.tur`, which
derives a codec for a `Job` struct, pushes it over `inproc://`, decodes it on
the other side, and asserts the wire carries exactly the encoder's bytes:

```turmeric
(defstruct Job [id : int  kind : cstr  urgent : bool])
(derive-msgpack Job (id int) (kind cstr) (urgent bool))

(let [encoded (encode-mp job)          ;; msgpack Buf
      wire    (mp->nng encoded)]       ;; -> nng Payload (one copy)
  (send-payload push wire)
  (payload-free wire)
  (buf-free encoded))
```

Both `mp->nng` and its inverse live in that test file, not in either spice: they
are ~6 lines of C against a layout both modules document, and they are what a
stdlib `Bytes` would eventually replace.

## Pub/sub and the slow joiner

A publisher does not queue for subscribers that are not connected yet, so the
first few messages after a `dial` are routinely dropped. This is nng behaving
correctly, not a bug in the spice or in your code. Publish on a loop, or
re-publish and re-try until something lands:

```turmeric
(sub-subscribe sub "")                 ;; "" = everything; a SUB socket with
(set-recv-timeout-ms sub 50)           ;;      NO subscription receives nothing
;; then re-send on the pub side and re-try the receive until one arrives
```

Topic matching is a raw byte **prefix**, not a namespace: subscribing to
`"temp"` also matches `"temperature"`.

## Concurrency

nng's blocking calls park only the CALLING thread -- nng's internal workers keep
running -- so a concurrent server is an ordinary thread running an ordinary
receive loop. Use `stdlib/thread.tur`:

```turmeric
;; one thread per REP worker; each runs a blocking recv/send loop
(thread-spawn (fn [] (serve-forever rep)))
```

That is the v0 concurrency answer, and it is the same one the
[`tur-valkey`](../valkey/) spice gives.

## Linear `Socket`

`Socket` is a `:linear` opaque. A socket extracted with `ok-val` must be
consumed exactly once, by `close`; every other operation takes it by `^borrow`,
observing it without discharging that obligation. Under `-Xsubstructural` that
makes three mistakes compile-time errors rather than runtime faults:

| Mistake | Diagnostic |
| --- | --- |
| closing the same socket twice | `TUR-E0101` linear value used after being consumed |
| operating on a closed socket | `TUR-E0101` linear value used after being consumed |
| opening a socket and never closing it | `TUR-E0100` linear value dropped without being consumed |

The discipline is inert in ordinary builds, so call sites compile unchanged.
`errors/` holds one rejected fixture per row, and `errors/run.sh` asserts each
one fails for its own reason.

## Operations with no result value

`dial`, `listen`, `sub-subscribe`, and the timeout setters return
`(Result nil int)`. `nil` is the ok payload that says "it worked and carries
nothing", so inspect these with `ok?` / `err?` alone -- there is no value to
read out, and the type says so.

This is what the spice's plan specified from the start. It briefly shipped a
`(defopaque Ack :int)` stand-in instead, because a `nil` ok payload emitted a
`void` union member and the monomorph would not compile
([report](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/result-nil-ok-payload-emits-void-field.md),
fixed upstream). The alternative it avoided -- `(Result int int)` with an
"ok carries 0" convention -- is the `:int` stand-in the house rules exist to
prevent.

## Not in v0

Deliberate omissions, each with a known seam:

- **`nng_aio` async and `nng_ctx` contexts.** The concurrent-server story is
  blocking calls on OS threads, as above.
- **Reactor integration.** nng exposes pollable receive/send file descriptors
  via socket options, which would plug into `stdlib/reactor.tur`. Documented,
  not built.
- **TLS, WebSocket, and ZeroTier transports.** `NNG_ENABLE_TLS=OFF` keeps
  mbedTLS out of the build entirely.
- **Per-protocol socket types.** Ten opaques would turn "receive on a PUB
  socket" into a compile error, which is the right end state -- but it
  multiplies every shared operation by ten or needs a typeclass over socket
  kinds, and that design deserves its own pass.
- **Zero-copy `nng_msg`.** One ownership model across the spice beats a faster
  path nothing needs yet.
- **Explicit dialer/listener handles** and their option tuning. `dial` and
  `listen` use nng's convenience forms, which tie the endpoint's lifetime to the
  socket's.

## Testing

```sh
tur fetch --update      # clone + build nng (once)
tur test tests/nng      # 52 assertions, all over inproc:// -- no network
errors/run.sh           # the three compile-fail linear fixtures
```

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/nng-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/nng>
- [`tur-msgpack`](../msgpack/) -- the binary codec whose buffer layout this
  spice shares
- [`tur-valkey`](../valkey/) -- the structural model for this spice
