---
title: Valkey and Redis Client
category: Data
description: Connect, run commands, walk reply trees, and subscribe to pub/sub -- a linear Client handle over hiredis, speaking the RESP protocol
audience: developers building caching, rate limiting, ephemeral state, or pub/sub messaging in Turmeric services
since: valkey v0.1.0
---

# tur-valkey Guide

Valkey and Redis are in-memory data stores that speak the same wire
protocol (RESP). This spice wraps hiredis in a small surface: a linear
`Client` handle, a generic `cmd` plus typed helpers, a recursive `reply`
accessor, and a `pubsub` module for subscribe/publish workflows.

This guide walks the five things you will do most often:

1. [Connecting and running commands](#1-connect)
2. [Typed command arguments](#2-typed-args)
3. [Walking reply trees](#3-replies)
4. [Hash and list commands](#4-hash-list)
5. [Pub/sub](#5-pubsub)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "valkey" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "valkey-v0.1.0"
             :subdir "spices/valkey"}
}
```

Then `tur fetch`. The spice is a `cmake-dep`: it builds hiredis statically.
Nothing to install on the host.

---

## The one idea

`Client` is a `:linear` opaque. A connection extracted with `ok-val` on
`client-connect` must be released exactly once with `client-close`
(`redisFree`). The command, pubsub, and ping operations take it by
`^borrow`, observing the connection without discharging that obligation.
Under substructural checking, use-after-close and connection leaks are
compile-time errors (`TUR-E0101` / `TUR-E0100`).

---

## 1. Connect

`client-connect` opens a TCP connection; `client-connect-unix` opens a
Unix domain socket. Both return `(Result Client int)`:

```turmeric
(defmodule demo
  (import valkey/client :refer [client-connect client-close client-ping])
  (import valkey/cmd    :refer [cmd-set cmd-get])
  (import valkey/reply  :refer [reply-string reply-free])

  (defn main [] : int
    (let [c (ok-val (client-connect "127.0.0.1" 6379))]
      (cmd-set c "greeting" "hello")
      (let [r (cmd-get c "greeting")]
        (println (reply-string r))     ;; "hello"
        (reply-free r))
      (client-close c))
    0))
```

```sweet-exp
defmodule demo
  import valkey/client :refer [client-connect client-close client-ping]
  import valkey/cmd    :refer [cmd-set cmd-get]
  import valkey/reply  :refer [reply-string reply-free]

  defn main [] : int
    let [c ok-val(client-connect("127.0.0.1" 6379))]
      cmd-set(c "greeting" "hello")
      let [r cmd-get(c "greeting")]
        println $ reply-string r      ;; "hello"
        reply-free(r)
      client-close(c)
    0
```

`client-ping` sends a PING and verifies the server responds with a status
reply -- useful as a health check.

---

## 2. Typed arguments

The typed helpers (`cmd-set`, `cmd-get`, `cmd-del`, `cmd-incr`,
`cmd-expire`, `cmd-hset`, `cmd-hget`, `cmd-lpush`, `cmd-lrange`) take
positional `cstr` parameters and are the convenience path.

The generic `cmd` takes a typed variadic of `ValkeyArg` values. Build each
argument with `vk-str`, `vk-int`, or `vk-bytes` so a value of the wrong
kind is a compile error at the call site instead of a downstream
`WRONGTYPE` reply or a segfault:

```turmeric
(defmodule demo
  (import valkey/client :refer [client-connect client-close])
  (import valkey/cmd    :refer [cmd vk-str vk-int vk-bytes])
  (import valkey/reply  :refer [reply-free reply-int reply-string])

  (defn main [] : int
    (let [c (ok-val (client-connect "127.0.0.1" 6379))]
      ;; string args
      (let [r (cmd c "SET" (vk-str "mykey") (vk-str "myval"))]
        (if (ok? r) (reply-free (ok-val r)) (println "set failed")))
      ;; integers are formatted to decimal on the wire
      (let [r (cmd c "EXPIRE" (vk-str "mykey") (vk-int 60))]
        (if (ok? r) (reply-free (ok-val r)) (println "expire failed")))
      ;; vk-bytes is binary-safe: embedded NULs flow through untruncated
      ;; (cmd c "SET" (vk-str "blob") (vk-bytes payload n))
      (client-close c))
    0))
```

```sweet-exp
defmodule demo
  import valkey/client :refer [client-connect client-close]
  import valkey/cmd    :refer [cmd vk-str vk-int vk-bytes]
  import valkey/reply  :refer [reply-free reply-int reply-string]

  defn main [] : int
    let [c ok-val(client-connect("127.0.0.1" 6379))]
      ;; string args
      let [r cmd(c "SET" vk-str("mykey") vk-str("myval"))]
        if ok?(r) reply-free(ok-val r) println("set failed")
      ;; integers are formatted to decimal on the wire
      let [r cmd(c "EXPIRE" vk-str("mykey") vk-int(60))]
        if ok?(r) reply-free(ok-val r) println("expire failed")
      ;; vk-bytes is binary-safe: embedded NULs flow through untruncated
      ;; cmd(c "SET" vk-str("blob") vk-bytes(payload n))
      client-close(c)
    0
```

`(cmd c "SET" "mykey" "myval")` -- passing bare cstrs -- does **not**
type-check: `expected ValkeyArg, got cstr`. Wrap each argument with the
appropriate `vk-*` constructor.

---

## 3. Replies

Every command returns `(Result Reply int)`. The `Reply` handle wraps a
`redisReply*`; inspect it with `reply-type`, `reply-string`, `reply-int`,
`reply-array-len`, and `reply-array-get`, then release it with
`reply-free`:

```turmeric
(defmodule demo
  (import valkey/client :refer [client-connect client-close])
  (import valkey/cmd    :refer [cmd-incr])
  (import valkey/reply  :refer [reply-type reply-int reply-free])

  (defn main [] : int
    (let [c (ok-val (client-connect "127.0.0.1" 6379))]
      (let [r (cmd-incr c "counter")]
        (if (ok? r)
          (let [rep (ok-val r)]
            (println (reply-type rep))   ;; "integer"
            (println (reply-int rep))     ;; 1
            (reply-free rep))
          (println "incr failed")))
      (client-close c))
    0))
```

```sweet-exp
defmodule demo
  import valkey/client :refer [client-connect client-close]
  import valkey/cmd    :refer [cmd-incr]
  import valkey/reply  :refer [reply-type reply-int reply-free]

  defn main [] : int
    let [c ok-val(client-connect("127.0.0.1" 6379))]
      let [r cmd-incr(c "counter")]
        if ok?(r)
          let [rep ok-val r]
            println $ reply-type rep   ;; "integer"
            println $ reply-int rep    ;; 1
            reply-free(rep)
          println("incr failed")
      client-close(c)
    0
```

`reply-type` returns one of `"string"`, `"integer"`, `"array"`, `"nil"`,
`"error"`, `"status"`, `"unknown"`. `reply-string` returns the string
content of a string, status, or error reply (empty for other types).
`reply-int` returns the integer value of an integer reply.

### Array replies

`reply-array-len` and `reply-array-get` walk array replies. Sub-replies
returned by `reply-array-get` are owned by the parent reply -- do not
call `reply-free` on them individually; free the parent once:

```turmeric no-check
(let [r (cmd-lrange c "mylist" 0 -1)]
  (when (ok? r)
    (let [rep (ok-val r)
          n   (reply-array-len rep)]
      ;; iterate: (reply-string (reply-array-get rep i)) for i in [0, n)
      (reply-free rep))))
```

---

## 4. Hash and list commands

The typed helpers cover the common hash and list operations:

```turmeric
(defmodule demo
  (import valkey/client :refer [client-connect client-close])
  (import valkey/cmd    :refer [cmd-hset cmd-hget cmd-lpush cmd-lrange])
  (import valkey/reply  :refer [reply-string reply-array-len reply-array-get
                               reply-free])

  (defn main [] : int
    (let [c (ok-val (client-connect "127.0.0.1" 6379))]
      ;; Hash: store and retrieve a field
      (let [r (cmd-hset c "user:1" "name" "Alice")]
        (when (ok? r) (reply-free (ok-val r))))
      (let [r (cmd-hget c "user:1" "name")]
        (when (ok? r)
          (println (reply-string (ok-val r)))  ;; "Alice"
          (reply-free (ok-val r))))
      ;; List: push and range
      (let [r (cmd-lpush c "mylist" "item")]
        (when (ok? r) (reply-free (ok-val r))))
      (let [r (cmd-lrange c "mylist" 0 -1)]
        (when (ok? r)
          (let [rep (ok-val r)]
            (println (reply-array-len rep))    ;; 1
            (reply-free rep))))
      (client-close c))
    0))
```

```sweet-exp
defmodule demo
  import valkey/client :refer [client-connect client-close]
  import valkey/cmd    :refer [cmd-hset cmd-hget cmd-lpush cmd-lrange]
  import valkey/reply  :refer [reply-string reply-array-len reply-array-get
                               reply-free]

  defn main [] : int
    let [c ok-val(client-connect("127.0.0.1" 6379))]
      ;; Hash: store and retrieve a field
      let [r cmd-hset(c "user:1" "name" "Alice")]
        when ok?(r) reply-free(ok-val r)
      let [r cmd-hget(c "user:1" "name")]
        when ok?(r)
          println $ reply-string $ ok-val r  ;; "Alice"
          reply-free $ ok-val r
      ;; List: push and range
      let [r cmd-lpush(c "mylist" "item")]
        when ok?(r) reply-free(ok-val r)
      let [r cmd-lrange(c "mylist" 0 -1)]
        when ok?(r)
          let [rep ok-val r]
            println $ reply-array-len rep    ;; 1
            reply-free(rep)
      client-close(c)
    0
```

| Helper | Command |
|--------|---------|
| `cmd-get c key` | GET |
| `cmd-set c key val` | SET |
| `cmd-del c key` | DEL |
| `cmd-incr c key` | INCR |
| `cmd-expire c key seconds` | EXPIRE |
| `cmd-hset c hash field val` | HSET |
| `cmd-hget c hash field` | HGET |
| `cmd-lpush c list val` | LPUSH |
| `cmd-lrange c list start stop` | LRANGE |

For anything not covered, use the generic `cmd` with `vk-*` arguments.

---

## 5. Pub/sub

Use a **dedicated connection** for pub/sub -- once a connection enters
subscribe mode, only SUBSCRIBE, UNSUBSCRIBE, PING, and RESET are valid.
Do not mix with cmd operations.

```turmeric
(defmodule demo
  (import valkey/client :refer [client-connect client-close])
  (import valkey/pubsub :refer [pubsub-subscribe pubsub-publish pubsub-recv
                               message-channel message-payload])

  (defn main [] : int
    ;; Publisher connection
    (let [pub (ok-val (client-connect "127.0.0.1" 6379))]
      ;; Subscriber connection (dedicated)
      (let [sub (ok-val (client-connect "127.0.0.1" 6379))]
        (pubsub-subscribe sub "events")
        (pubsub-publish pub "events" "hello")
        (let [r (pubsub-recv sub)]
          (when (ok? r)
            (let [m (ok-val r)]
              (when (!= (:: m :int) 0)
                (println (message-channel m))   ;; "events"
                (println (message-payload m))))) ;; "hello"
        (client-close sub))
      (client-close pub))
    0))
```

```sweet-exp
defmodule demo
  import valkey/client :refer [client-connect client-close]
  import valkey/pubsub :refer [pubsub-subscribe pubsub-publish pubsub-recv
                               message-channel message-payload]

  defn main [] : int
    ;; Publisher connection
    let [pub ok-val(client-connect("127.0.0.1" 6379))]
      ;; Subscriber connection (dedicated)
      let [sub ok-val(client-connect("127.0.0.1" 6379))]
        pubsub-subscribe(sub "events")
        pubsub-publish(pub "events" "hello")
        let [r pubsub-recv(sub)]
          when ok?(r)
            let [m ok-val r]
              when (!= (:: m :int) 0)
                println $ message-channel m   ;; "events"
                println $ message-payload m   ;; "hello"
        client-close(sub)
      client-close(pub)
    0
```

`pubsub-recv` blocks until a message arrives. Non-message pushes (subscribe
confirmations) return `ok` carrying 0 rather than a `Message` handle.
`pubsub-publish` returns the subscriber count as its ok value.

---

## When not to use this

- **You need connection pooling or async I/O.** v0 is a single blocking
  connection per `Client`. Use threads for concurrency, same as `tur-nng`.
- **You need RESP3 or cluster mode.** hiredis speaks RESP2; cluster support
  is not built.
- **You need streaming replies or pipelining.** Each command is a
  synchronous request/reply. Batch with threads, not with a pipeline API.

---

## See also

- [API reference](api/)
- [README](../../spices/valkey/README.md) -- the typed argument surface and linear Client model
- [tur-nng guide](nng-guide.html) -- the structural model for this spice
