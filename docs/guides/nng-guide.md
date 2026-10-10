---
title: Scalability Protocols with nng
category: Networking
description: Request/reply, pub/sub, pipeline, pair, bus, and survey messaging over inproc, ipc, and tcp -- blocking calls over a linear socket handle, poll fds for event loops, completion-based async (nng/aio), and per-conversation contexts (nng/ctx)
audience: developers building distributed or inter-process messaging in Turmeric
since: nng v0.1.0
---

# tur-nng Guide

A byte stream gives you send and receive. A messaging pattern gives you a
contract: one request yields exactly one reply, a job is handed to exactly
one worker, a subscriber sees every message whose topic it matches. nng
(nanomsg-next-generation) implements those patterns; this spice wraps them
in a thin surface of blocking calls over one linear `Socket` handle.

This guide walks the nine things you will do most often:

1. [Request/reply: the RPC round trip](#1-requestreply)
2. [Pipeline: a load-balanced job queue](#2-pipeline)
3. [Pub/sub: topic-filtered fan-out](#3-pubsub)
4. [Binary payloads and msgpack interop](#4-binary-payloads)
5. [Timeouts: not hanging forever](#5-timeouts)
6. [Concurrency: blocking calls on threads](#6-concurrency)
7. [Event loops: poll fds and try-*](#7-event-loops)
8. [Async: submit now, collect later](#8-async-nngaio)
9. [Contexts: many conversations on one socket](#9-contexts-nngctx)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "nng" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "nng-v0.1.0"
          :subdir "spices/nng"}
}
```

Then `tur fetch`. The spice is a `cmake-dep`: it clones and builds nng
statically (TLS off, test targets off), so nothing needs to be installed on
the host.

---

## The one idea

Every protocol -- req/rep, pub/sub, push/pull, pair, bus, survey -- shares
one `Socket` type. The protocol is chosen by which constructor opens the
socket, not by a type parameter:

```turmeric no-check
(let [req (ok-val (req-open))]   ;; speaks REQ
      (rep (ok-val (rep-open)))] ;; speaks REP
  ...)
```

A socket talks only to a peer speaking the matching half. Sending on a
receive-only socket is a runtime `err` carrying `NNG_ENOTSUP`, not a
compile error -- per-protocol socket types are a deliberate non-goal for v0.

`Socket` is `:linear`: a socket extracted with `ok-val` must be consumed
exactly once, by `close`. Every other operation borrows it. Under
substructural checking, double-close, use-after-close, and a leaked socket
are compile-time errors (`TUR-E0101` / `TUR-E0100`).

---

## 1. Request/reply

The client half sends a request and receives exactly one reply; nng resends
the request automatically if the reply does not arrive. The server half
receives a request and sends exactly one reply back to whoever asked.

```turmeric
(defmodule demo
  (import nng/socket :refer [req-open rep-open dial listen close
                             set-recv-timeout-ms])
  (import nng/msg    :refer [send-str recv-str])

  (defn main [] : int
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
    0))
```

```sweet-exp
defmodule demo
  import nng/socket :refer [req-open rep-open dial listen close
                           set-recv-timeout-ms]
  import nng/msg    :refer [send-str recv-str]

  defn main [] : int
    let [rep ok-val(rep-open())
         req ok-val(req-open())]
      listen(rep "inproc://demo")
      dial(req "inproc://demo")
      set-recv-timeout-ms(rep 2000)
      set-recv-timeout-ms(req 2000)

      send-str(req "ping")
      let [asked recv-str(rep)]
        when ok?(asked)
          println $ ok-val asked       ;; "ping"
          send-str(rep "pong")
      let [heard recv-str(req)]
        when ok?(heard)
          println $ ok-val heard       ;; "pong"

      close(req)
      close(rep)
    0
```

`inproc://` connects two sockets inside one process with no network and no
port. The same code moves to `tcp://` by changing the string -- which is
what makes the entire test suite network-free and CI-safe.

---

## 2. Pipeline

A PUSH socket sends; each message goes to exactly one connected PULL peer,
round-robin. Add PULL workers to go faster.

```turmeric
(defmodule demo
  (import nng/socket :refer [push-open pull-open dial listen close
                             set-recv-timeout-ms])
  (import nng/msg    :refer [send-str recv-str])

  (defn main [] : int
    (let [push (ok-val (push-open))
          pull (ok-val (pull-open))]
      (listen push "inproc://jobs")
      (dial   pull "inproc://jobs")
      (set-recv-timeout-ms pull 2000)

      (send-str push "job:42")
      (let [r (recv-str pull)]
        (when (ok? r)
          (println (ok-val r))))         ;; "job:42"

      (close pull)
      (close push))
    0))
```

```sweet-exp
defmodule demo
  import nng/socket :refer [push-open pull-open dial listen close
                           set-recv-timeout-ms]
  import nng/msg    :refer [send-str recv-str]

  defn main [] : int
    let [push ok-val(push-open())
         pull ok-val(pull-open())]
      listen(push "inproc://jobs")
      dial(pull "inproc://jobs")
      set-recv-timeout-ms(pull 2000)

      send-str(push "job:42")
      let [r recv-str(pull)]
        when ok?(r)
          println $ ok-val r           ;; "job:42"

      close(pull)
      close(push)
    0
```

---

## 3. Pub/sub

A publisher does not queue for subscribers that are not connected yet, so
the first few messages after a `dial` are routinely dropped. This is nng
behaving correctly. A SUB socket with no subscription receives nothing at
all -- call `sub-subscribe` with `""` to take everything:

```turmeric
(defmodule demo
  (import nng/socket :refer [pub-open sub-open dial listen close
                             set-recv-timeout-ms])
  (import nng/msg    :refer [send-str recv-str sub-subscribe])

  (defn main [] : int
    (let [pub (ok-val (pub-open))
          sub (ok-val (sub-open))]
      (listen pub "inproc://feed")
      (dial   sub "inproc://feed")
      (sub-subscribe sub "")            ;; "" = every topic
      (set-recv-timeout-ms sub 2000)

      (send-str pub "temp:21.5")
      (let [r (recv-str sub)]
        (when (ok? r)
          (println (ok-val r))))        ;; "temp:21.5"

      (close sub)
      (close pub))
    0))
```

```sweet-exp
defmodule demo
  import nng/socket :refer [pub-open sub-open dial listen close
                           set-recv-timeout-ms]
  import nng/msg    :refer [send-str recv-str sub-subscribe]

  defn main [] : int
    let [pub ok-val(pub-open())
         sub ok-val(sub-open())]
      listen(pub "inproc://feed")
      dial(sub "inproc://feed")
      sub-subscribe(sub "")            ;; "" = every topic
      set-recv-timeout-ms(sub 2000)

      send-str(pub "temp:21.5")
      let [r recv-str(sub)]
        when ok?(r)
          println $ ok-val r          ;; "temp:21.5"

      close(sub)
      close(pub)
    0
```

Topic matching is a raw byte **prefix**, not a namespace: subscribing to
`"temp"` also matches `"temperature"`.

### Picking a protocol

| Pattern | Constructors | Shape | Reach for it when |
|---------|-------------|-------|-------------------|
| Request/reply | `req-open` / `rep-open` | one request, one reply, retried | RPC; the caller needs an answer |
| Pipeline | `push-open` / `pull-open` | each message to one peer, round-robin | a job queue; add workers to go faster |
| Pub/sub | `pub-open` / `sub-open` | every message to every matching subscriber | live feeds, telemetry, notifications |
| Pair | `pair-open` | two peers, both directions, no turn-taking | a dedicated link between two components |
| Bus | `bus-open` | each message to every directly connected peer, one hop | small peer groups that all see everything |
| Survey | `surveyor-open` / `respondent-open` | one question broadcast, many answers, bounded by deadline | service discovery, quorum polls |

### Addresses

| Scheme | Example | Notes |
|--------|---------|-------|
| `inproc://` | `inproc://jobs` | same process, no network; the name is a bare label, not a path |
| `ipc://` | `ipc:///tmp/jobs.sock` | same machine, Unix domain socket (named pipe on Windows) |
| `tcp://` | `tcp://0.0.0.0:5555` to listen, `tcp://host:5555` to dial | across machines |

`listen` binds and fails immediately if the address is taken; `dial`
connects and reconnects in the background if the peer goes away.

---

## 4. Binary payloads

`send-str` / `recv-str` carry text. The message must not contain NUL bytes
-- `recv-str` terminates the string it hands back, so an interior NUL
truncates it.

`send-payload` / `recv-payload` are the real path. `Payload` is a heap
block laid out as `{ int64 len; uint8 data[] }`, so binary messages cross
intact:

```turmeric
(defmodule demo
  (import nng/socket  :refer [push-open pull-open dial listen close
                              set-recv-timeout-ms])
  (import nng/payload :refer [payload-of-cstr payload-free payload->hex])
  (import nng/msg     :refer [send-payload recv-payload])

  (defn main [] : int
    (let [push (ok-val (push-open))
          pull (ok-val (pull-open))]
      (listen push "inproc://bin")
      (dial   pull "inproc://bin")
      (set-recv-timeout-ms pull 2000)

      (let [b (payload-of-cstr "job:42")]
        (send-payload push b)
        (payload-free b))               ;; send BORROWS; the Payload is still yours

      (let [r (recv-payload pull)]
        (when (ok? r)
          (println (payload->hex (ok-val r)))
          (payload-free (ok-val r))))    ;; recv gives you a fresh one to free

      (close pull)
      (close push))
    0))
```

```sweet-exp
defmodule demo
  import nng/socket  :refer [push-open pull-open dial listen close
                              set-recv-timeout-ms]
  import nng/payload :refer [payload-of-cstr payload-free payload->hex]
  import nng/msg     :refer [send-payload recv-payload]

  defn main [] : int
    let [push ok-val(push-open())
         pull ok-val(pull-open())]
      listen(push "inproc://bin")
      dial(pull "inproc://bin")
      set-recv-timeout-ms(pull 2000)

      let [b payload-of-cstr("job:42")]
        send-payload(push b)
        payload-free(b)               ;; send BORROWS; the Payload is still yours

      let [r recv-payload(pull)]
        when ok?(r)
          println $ payload->hex $ ok-val r
          payload-free $ ok-val r     ;; recv gives you a fresh one to free

      close(pull)
      close(push)
    0
```

### Why Payload and not Buf

The layout is deliberately identical to `tur-msgpack`'s `Buf` (and to
stdlib `serial.tur`'s bytes value) -- the same `{ int64 len; uint8 data[] }`
block from the same `malloc`. The **name** differs because an opaque
resolves globally: two spices that each define a `Buf` cannot both be
loaded by one program, and msgpack-over-nng is the pairing both spices'
plans were written for.

With the names distinct, crossing between them is a copy against a
documented layout rather than a reinterpret. See `tests/nng/msgpack_test.tur`,
which derives a codec for a `Job` struct, pushes it over `inproc://`, decodes
it on the other side, and asserts the wire carries exactly the encoder's
bytes.

---

## 5. Timeouts

Set a receive timeout on anything that might not get an answer. Without
one, a receive on a quiet socket parks the calling thread forever:

```turmeric
(set-recv-timeout-ms s 100)
(let [r (recv-str s)]
  (if (ok? r)
    (handle (ok-val r))
    (if (timed-out? (err-val r))
      (nothing-yet)                    ;; not a failure -- just nothing to read
      (println (err-str (err-val r))))))
```

```sweet-exp
set-recv-timeout-ms(s 100)
let [r recv-str(s)]
  if ok?(r)
    handle $ ok-val r
    if timed-out?(err-val(r))
      nothing-yet()                    ;; not a failure -- just nothing to read
      println $ err-str $ err-val r
```

`timed-out?` is the one error code worth branching on; `err-str` renders
any of them via `nng_strerror`.

---

## 6. Concurrency

nng's blocking calls park only the calling thread -- nng's internal workers
keep running -- so a concurrent server is ordinary `stdlib/thread.tur`
threads running ordinary receive loops, the same answer `tur-valkey` gives.

One worker per socket needs nothing more. Several workers on **one** REP
socket need a context each (section 9): the socket itself has one receive
state, so a second worker's receive fails with `NNG_ESTATE`. To multiplex
many sockets on one thread instead, use an event loop (next section), or
completion-based async (section 8).

---

## 7. Event loops

`stdlib/reactor.tur` runs one thread's worth of sockets. Each socket hands
the reactor a poll fd, and the callback moves messages with calls that never
block:

```turmeric no-check
(import nng/socket :refer [Socket sub-open dial close
                           recv-poll-fd poll-fd->int])
(import nng/msg    :refer [sub-subscribe try-recv-str])
(import reactor)

;; drain -- every waiting message, never a blocking receive.
(defn drain [^borrow s : Socket n : int] : int
  (let [r (try-recv-str s)]
    (if (ok? r)
      (match (ok-val r)
        (Some m) (do (println m) (drain s (+ n 1)))
        (None)   n)
      n)))

(let [sub (ok-val (sub-open))
      _   (dial sub "inproc://feed")
      _   (sub-subscribe sub "")
      r   (reactor-new)
      fd  (ok-val (recv-poll-fd sub))]
  (reactor-add-fd r (poll-fd->int fd) READ
    (fn [id events user] : nil (drain sub 0))
    (:: 0 :ptr<void>))
  (reactor-run r)
  (reactor-free r)
  (close sub))
```

The pieces:

- **`recv-poll-fd` / `send-poll-fd`** return a `PollFd`, the read end of a
  pipe nng raises while the socket is ready. It is poll-only: never read,
  write or close it. **Both** fds signal by becoming readable, so register
  both for `READ`.
- **`try-recv-str` / `try-recv-payload`** return `(Result (Option T) int)`.
  `(ok (none))` means nothing is waiting; an `err` is a real failure, never
  "nothing yet".
- **`try-send-str` / `try-send-payload`** return `(Result bool int)`.
  `(ok false)` means the send would block and nothing was sent.

**Drain to empty.** On macOS the reactor reports the fd once per
empty-to-ready transition (kqueue `EV_CLEAR`). A callback that takes one
message per wakeup stalls with the rest still queued. Draining is harmless on
Linux, so always drain.

PUB and PUSH have no receive fd, and SUB and PULL have no send fd. Asking for
one is an `err` carrying `NNG_ENOTSUP`. Do not use poll fds on a socket that
also uses nng contexts.

---

## 8. Async (`nng/aio`)

Section 7 asks "is the socket ready?" and then moves a message itself.
`nng/aio` asks nng to move it and says when it is done: submit an operation
now and collect the outcome later. One `Aio` is one operation slot, and N
slots in flight on one thread are the fan-out.

```turmeric no-check
(import nng/aio :refer [Aio aio-alloc aio-free recv-aio send-str-aio
                        aio-wait aio-try-result aio-take-str aio-poll-fd])

(let [a (ok-val (aio-alloc))
      r (recv-aio pull a)          ;; returns at once: (ok nil) = submitted
      w (aio-wait a)]              ;; blocks this thread until it finishes
  (when (ok? w)
    (let [t (aio-take-str a)]      ;; the received text, now yours
      (when (ok? t) (println (ok-val t)))))
  (aio-free a))
```

`Aio` is `:linear`: it must reach `aio-free` exactly once, and every other
call borrows it. Submit with `send-payload-aio`, `send-str-aio` or
`recv-aio`; then collect in whichever way suits the caller:

| Collector | Behaviour |
| --- | --- |
| `aio-wait` | blocks this thread, returns `(Result nil int)` |
| `aio-try-result` | never blocks: `(none)` while in flight, else `(some outcome)` |
| `aio-poll-fd` | a `PollFd` readable once it finishes |

The poll fd plugs into both event-loop shapes from section 7. With a reactor,
the callback collects:

```turmeric no-check
(let [a  (ok-val (aio-alloc))
      rs (recv-aio pull a)
      fd (ok-val (aio-poll-fd a))
      r  (reactor-new)]
  (reactor-add-fd r (poll-fd->int fd) READ
    (fn [id events user] : nil
      (let [t (aio-take-str a)]            ;; collecting clears the fd
        (when (ok? t) (println (ok-val t)))
        (reactor-stop r)))
    (:: 0 :ptr<void>))
  (reactor-run r)
  (reactor-free r)                         ;; the source goes before the fd
  (aio-free a))
```

In direct style, a `LocalFiberGroup` fiber parks on it:
`(local-park-fd g (poll-fd->int fd) READ timeout-ms)`, then takes the message
when it resumes.

What the slot does for you, on top of nng's own `nng_aio`:

- **No `nng_msg` reaches your code or leaks.** A send copies the payload in,
  so you still own the `Payload`. A failed send's message is freed for you,
  and so is an untaken receive, by the next submit or by `aio-free`.
- **One operation at a time.** A submit while one is in flight is
  `(err NNG_EBUSY)` and nothing is submitted. nng itself asserts here.
- **`aio-free` is safe in flight.** It cancels and waits first.
- **The fd stays readable until you collect.** Any collector that sees the
  result clears it, and so does the next submit, so a reactor callback that
  collects neither spins (epoll) nor stalls (kqueue).
- **Timeouts follow the socket.** By default each operation takes the
  socket's `set-recv-timeout-ms` / `set-send-timeout-ms` as they stand when
  it is submitted. `aio-set-timeout` overrides that for the slot.

`aio-cancel` asks an operation to stop; it then completes with
`NNG_ECANCELED`. An `Aio` belongs to one thread at a time. nng's completion
callback runs on one of nng's own threads, and the spice's C callback never
calls a Turmeric closure from there.

---

## 9. Contexts (`nng/ctx`)

REQ and REP are stateful. A REQ socket remembers the request it is waiting
on, and a REP socket remembers whom it owes a reply. The socket's own calls
share that one state machine. On REQ a second request cancels the first; on
REP a second pending receive fails with `NNG_ESTATE`. So one REQ socket has
one request out, and one REP socket serves one request at a time.

An `NngCtx` is an independent copy of that state on the socket. N contexts
are N conversations, each with its own receive-then-reply (REP) or
request-then-reply (REQ) cycle:

```turmeric no-check
(import nng/ctx :refer [NngCtx ctx-open ctx-close ctx-recv-str ctx-send-str])

;; serve-one -- one conversation on a REP context.
(defn serve-one [^borrow c : NngCtx] : nil
  (let [r (ctx-recv-str c)]
    (when (ok? r)
      (let [s (ctx-send-str c "pong")]
        (when (err? s) (println "reply failed"))))))

(let [c (ok-val (ctx-open rep))]
  (serve-one c)
  (ctx-close c))
```

`NngCtx` is `:linear`: `ctx-close` consumes it, and every other call borrows
it. Contexts come in two flavours, matching the two ways to be concurrent:

| Flavour | Calls | Concurrency |
| --- | --- | --- |
| Blocking | `ctx-send-str`, `ctx-recv-str`, `ctx-send-payload`, `ctx-recv-payload` | one thread per context |
| Async | `ctx-send-str-aio`, `ctx-send-payload-aio`, `ctx-recv-aio` | many contexts on one thread, via an `Aio` each |

**Threads.** N worker threads on one REP socket, each blocked in
`ctx-recv-str` on its own context, is the classic concurrent server. A
context belongs to one thread at a time, so hand each worker its context
rather than sharing one. `tests/nng/ctx_test.tur` opens each context on the
main thread and moves it into its worker through the thread's argument. The
worker serves on it and closes it.

**One thread.** The async calls follow section 8's contract. Collect with
`aio-wait`, `aio-try-result` or `aio-poll-fd`; take a receive with
`aio-take-str` / `aio-take-payload`. Two requests out at once on one REQ
socket:

```turmeric no-check
(let [s1 (ctx-send-str-aio q1 a1 "q1")    ;; q1, q2: contexts on one REQ socket
      s2 (ctx-send-str-aio q2 a2 "q2")
      w1 (aio-wait a1)                    ;; both requests accepted
      w2 (aio-wait a2)
      r1 (ctx-recv-aio q1 a1)
      r2 (ctx-recv-aio q2 a2)
      v1 (aio-wait a1)                    ;; both replies in
      v2 (aio-wait a2)]
  (aio-take-str a1))                      ;; q1's reply, whatever order REP answered in
```

The rules:

- Contexts exist on REQ, REP, SUB, SURVEYOR and RESPONDENT. `ctx-open` on
  PUB, PUSH, PULL, PAIR or BUS is `(err NNG_ENOTSUP)`.
- **SUB contexts filter on their own.** Use `ctx-subscribe` /
  `ctx-unsubscribe`. A context with no subscription receives nothing,
  whatever its socket or its sibling contexts subscribe to.
- **Timeouts.** A context copies its socket's timeouts at `ctx-open`. Change
  its own with `ctx-set-recv-timeout-ms` / `ctx-set-send-timeout-ms`. An
  `Aio` on a context takes the context's timeout unless `aio-set-timeout`
  overrides it.
- **Poll fds or contexts, never both,** on one socket (nng's rule).
- **Close each context before its socket.** The other order is harmless,
  because closing the socket reaps its contexts. Until then, every call on
  such a context is `(err NNG_ECLOSED)`.

---

## Operations with no result value

`dial`, `listen`, `sub-subscribe`, and the timeout setters return
`(Result nil int)`. `nil` is the ok payload that says "it worked and carries
nothing", so inspect these with `ok?` / `err?` alone -- there is no value to
read out, and the type says so.

---

## When not to use this

- **You need TLS, WebSocket, or ZeroTier transports.** TLS is off in the
  build (`NNG_ENABLE_TLS=OFF`); the other transports are not compiled in.
- **You want per-protocol socket types.** Ten opaques would turn "receive on
  a PUB socket" into a compile error, which is the right end state -- but it
  multiplies every shared operation by ten or needs a typeclass over socket
  kinds, and that design deserves its own pass.

---

## Tests

```sh
tur fetch --update      # clone + build nng (once)
tur test tests/nng      # 82 assertions in 8 suites, all over inproc:// -- no network
errors/run.sh           # the nine compile-fail linear fixtures (Socket, Aio, NngCtx)
```

---

## See also

- [API reference](api/)
- [README](../../spices/nng/README.md) -- the full protocol table and address reference
- [tur-msgpack guide](msgpack-guide.html) -- the binary codec whose buffer layout this spice shares
- [tur-valkey guide](valkey-guide.html) -- the structural model for this spice
