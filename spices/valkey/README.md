# tur-valkey

Valkey/Redis client for Turmeric via hiredis. Connect, run commands, walk
reply trees, and subscribe to pub/sub channels.

## Overview

`tur-valkey` is a `cmake-dep` spice that wraps hiredis. It speaks the
Valkey/Redis RESP protocol, so it works against both Valkey and any
Redis-compatible server. The surface is intentionally small: a `client`
handle, a generic `cmd` plus typed helpers (`cmd-get`, `cmd-set`,
`cmd-incr`, hash and list commands), a recursive `reply` accessor, and a
`pubsub` module for subscribe/publish workflows.

Use it for caching, rate limiting, ephemeral state, and pub/sub messaging
in Turmeric services.

## Install

```turmeric no-check
:spices {
  "valkey" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "valkey-v0.1.0"
            :subdir "spices/valkey"}
}
```

## Quick start

```turmeric
(import valkey/client :refer [client-connect client-close])
(import valkey/cmd    :refer [cmd-set cmd-get])
(import valkey/reply  :refer [reply-string reply-free])

(let [c (ok-val (client-connect "127.0.0.1" 6379))]
  (cmd-set c "greeting" "hello")
  (let [r (cmd-get c "greeting")]
    (println (reply-string r))
    (reply-free r))
  (client-close c))
```

```sweet-exp
#lang sweet-exp
import valkey/client :refer [client-connect client-close]
import valkey/cmd    :refer [cmd-set cmd-get]
import valkey/reply  :refer [reply-string reply-free]

let [c ok-val(client-connect("127.0.0.1" 6379))]
  cmd-set(c "greeting" "hello")
  let [r cmd-get(c "greeting")]
    println $ reply-string r
    reply-free(r)
  client-close(c)
```

### Typed `cmd` arguments (U6)

The generic `cmd` takes a typed variadic of `ValkeyArg` values rather than
an untyped argument list. Build each argument with `vk-str`, `vk-int`, or
`vk-bytes` so a value of the wrong kind (a bare `Client`, a stray cstr, a
freed reply pointer) is a compile error at the call site instead of a
downstream `WRONGTYPE` reply or a segfault:

```turmeric
(import valkey/cmd :refer [cmd vk-str vk-int vk-bytes])
(import valkey/reply :refer [reply-free])

;; string args
(let [r (cmd c "SET" (vk-str "mykey") (vk-str "myval"))]
  (if (ok? r) (reply-free (ok-val r)) (println "set failed")))

;; integers are formatted to decimal on the wire
(cmd c "EXPIRE" (vk-str "mykey") (vk-int 60))

;; vk-bytes is binary-safe: embedded NULs flow through untruncated
(cmd c "SET" (vk-str "blob") (vk-bytes payload n))
```

`(cmd c "SET" "mykey" "myval")` -- passing bare cstrs -- does **not**
type-check: `expected ValkeyArg, got cstr`. Wrap each argument with the
appropriate `vk-*` constructor. The typed helpers (`cmd-set`, `cmd-get`,
...) keep their positional `cstr` parameters and are unaffected.

### Linear `Client` (U1)

`Client` is a `:linear` opaque. A connection extracted with `ok-val` on
`client-connect` must be released exactly once with `client-close`
(`redisFree`); the command, pubsub, and ping operations take it by
`^borrow`, observing the connection without discharging that obligation.
Under `-Xsubstructural` this makes use-after-close and connection leaks
compile-time errors (`TUR-E0101` / `TUR-E0100`) instead of runtime faults.
The discipline is inert in ordinary builds, so existing call sites compile
unchanged. See `errors/` for the rejected cases.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/valkey-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/valkey>
