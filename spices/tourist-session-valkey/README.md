# tur-tourist-session-valkey

**Valkey-backed session store** for
[`tur-tourist-session`](../tourist-session).

The core `tourist-session` spice has no native dependencies and ships an
in-memory store and a file store. This sibling spice adds a third backend —
Valkey/Redis — without pulling the `valkey`/hiredis native dependency into
every session-using app. Reach for it once a single process's in-memory store
or one machine's file store no longer fits: multi-process, multi-server, or
restart-surviving deployments.

It plugs into the same `Store` vtable, so installation is identical to the
built-in stores:

```turmeric
(import valkey/client          :refer [client-connect client-close])
(import tourist-session-valkey/store :refer [valkey-store-new])
(import session/mw             :refer [session-mw])
(import session/config         :refer [default-session-config])

(defn main [] : int
  (let [c     (ok-val (client-connect "127.0.0.1" 6379))
        store (valkey-store-new c "sess" 86400)]   ;; client, key prefix, max-age
    (let [srv (tourist 3000
                (session-mw store (default-session-config))
                (get! "/" home))]
      ;; ... serve ...
      (client-close c)   ;; the store borrows the client; you own its lifecycle
      0)))
```

```sweet-exp
#lang sweet-exp
(import valkey/client          :refer [client-connect client-close])
(import tourist-session-valkey/store :refer [valkey-store-new])
(import session/mw             :refer [session-mw])
(import session/config         :refer [default-session-config])

defn main [] : int
  let [c     ok-val(client-connect("127.0.0.1" 6379))
       store valkey-store-new(c "sess" 86400)]   ;; client, key prefix, max-age
    let [srv tourist(3000
                     session-mw(store default-session-config())
                     get!("/" home))]
      ;; ... serve ...
      client-close(c)   ;; the store borrows the client; you own its lifecycle
      0
```

## How it stores sessions

- **Key layout.** Each session is one key, `<prefix>:<session-id>`.
- **Serialization.** The value is the same JSON envelope the file store
  writes (`{"expires_at":N,"data":{...}}`), produced by the shared
  `session/serde` codec in core `tourist-session`.
- **Expiry.** The key is given a TTL equal to the session's `max-age`, so
  expired sessions vacate on their own — no sweeper. A `max-age` of `0`
  ("until browser close") has no natural Valkey TTL, so the key falls back to a
  documented **24h** default.
- **Client lifecycle.** `valkey-store-new` **borrows** the client and never
  closes it. Keep it alive for the life of the server and `client-close` it on
  shutdown.

## API

```turmeric no-check
;; client  -- borrowed Valkey client (caller owns lifecycle)
;; prefix  -- key namespace, e.g. "sess"
;; max-age -- session lifetime in seconds (match SessionConfig's max-age)
(defn valkey-store-new [^borrow client : Client prefix : cstr max-age : int] : Store)
```

## Tests

`tests/` drives a real save → load → delete cycle against a Valkey on
`127.0.0.1:6379`. The test is **self-skipping**: with no server reachable it
prints a skip notice and passes, so `tur test` is green on a machine without
Valkey. To run it for real, start a server first:

```sh
valkey-server --port 6379 &     # or: redis-server
tur test tests/tourist-session-valkey
```
