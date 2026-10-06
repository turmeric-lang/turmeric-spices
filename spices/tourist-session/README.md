# tur-tourist-session

Swappable-store **session middleware** for the [`tur-tourist`](../tourist)
HTTP micro-framework.

- Reads / writes a session cookie on every request that touches the session
- Loads and saves session data through a **pluggable store** so the storage
  backend swaps without touching application code
- Ships two stores out of the box:
  - **memory store** — in-process, mutex-guarded; no external deps
  - **file store** — one JSON file per session; survives restarts
- A third, **Valkey/Redis store**, ships as the sibling spice
  [`tur-tourist-session-valkey`](../tourist-session-valkey) (keeps the native
  dep optional)
- **Optional HMAC-signed cookies**, **`session-rotate!`** for session-fixation
  defense, and **CSRF synchronizer-token** helpers

The API is modelled on Rack's session middleware, adapted to Turmeric's
ownership model and tourist's context pattern.

## Quick start

```turmeric
(import tourist/app          :refer [tourist])
(import tourist/dsl          :refer [get! post!])
(import tourist/helpers      :refer [text redirect])
(import session/mw           :refer [session-mw])
(import session/config       :refer [dev-session-config])
(import session/memory-store :refer [memory-store-new])
(import session/ctx          :refer [session-get session-set! session-destroy!])

(defn dashboard [ctx : Ctx] : Response
  (let [uid (session-get ctx "user_id")]
    (if (.is-ok uid)
      (text (str-concat "Hello, user " (.ok-val uid)))
      (redirect "/login"))))

(defn main [] : int
  (let [store (memory-store-new)
        srv   (tourist 3000
                (session-mw store (dev-session-config))   ;; drop it in the list
                (get! "/dashboard" dashboard))]
    (server-stop srv)
    0))
```

Swapping the backend is a one-line change:

```turmeric
(session-mw (file-store-new "/var/lib/app/sessions") (default-session-config))
```

A complete login/logout flow lives in
[`examples/login_app.tur`](examples/login_app.tur):

```sh
tur run spices/tourist-session/examples/login_app.tur
```

## How it works

`session-mw` does two things: it installs the store and cookie config into a
process-global (read once per request, set once before the server starts, just
like tourist's own dispatch state) and returns a tourist **after-middleware**
item. There is no separate "before" middleware — the session is loaded
**lazily** the first time a handler calls a `session-*` helper:

1. the configured cookie is read from the request;
2. if it names a session the store knows, that ID + map are adopted;
3. otherwise a fresh 256-bit ID and an empty map are minted.

On the way out, the after-middleware inspects the per-request session state:

| Situation | Action |
|-----------|--------|
| `session-destroy!` was called | `store-delete` + an expiring `Set-Cookie` |
| the session was written (dirty) | `store-save` + a fresh `Set-Cookie` |
| `rolling?` on, established session | `store-save` + `Set-Cookie` (slide expiry) |
| read-only / never touched | nothing — no needless writes or cookies |

Because loading is lazy, a request whose handler never touches the session
pays nothing and is handed no cookie.

## Public API

### Middleware

- `(session-mw store config)` → tourist item. Install once, place in the
  `tourist` list (not wrapped in `use!`).
- `(session-flush ctx resp)` → the after-middleware itself, exported so a
  non-tourist host can flush a session manually.

### Context helpers (call from handlers)

- `(session-get ctx key)` → `(Result cstr cstr)` — `ok` value or `err "missing"`
- `(session-set! ctx key val)` → mark dirty, store `val`
- `(session-del! ctx key)` → remove a key, mark dirty
- `(session-destroy! ctx)` → delete from the store + clear the cookie
- `(session-rotate! ctx)` → issue a fresh ID, carry the data forward (fixation
  defense; call after login)
- `(session-id ctx)` → the current session ID (64-char hex)

### Configuration

- `(default-session-config)` — production: `Secure`, `HttpOnly`, 24 h, `Lax`
- `(dev-session-config)` — development: `Secure` off (works over plain HTTP)
- `(with-signing-key cfg key)` — a copy of `cfg` with HMAC cookie signing on
- `SessionConfig` fields: `cookie-name`, `path`, `domain`, `max-age`,
  `secure?`, `http-only?`, `same-site`, `rolling?`, `signing-key`

### Stores

- `(memory-store-new)` → `Store`; `(memory-store-count store)` for tests
- `(file-store-new dir)` → `Store` (creates `dir` 0700 if absent)
- `(valkey-store-new client prefix max-age)` → `Store` — from the sibling spice
  [`tur-tourist-session-valkey`](../tourist-session-valkey)

### Signed cookies

By default the cookie is an unsigned opaque bearer token: a 256-bit random ID
the store maps to data, so a tampered or guessed cookie simply fails to look
up. Turn on HMAC-SHA256 signing to reject a tampered or forged cookie at
**parse time**, before any store lookup:

```turmeric
(import session/config :refer [default-session-config with-signing-key])

(session-mw store (with-signing-key (default-session-config) my-secret-key))
```

The cookie value becomes `<id>.<base64url-hmac>`; on the way in the MAC is
recomputed and **constant-time** compared. A mismatch is treated exactly like
an unknown ID — a fresh session is minted. Signing is **fail-closed**: turning
it on rejects every previously-issued unsigned cookie, so existing sessions are
invalidated on deploy. The key is a single key (`""` disables signing); a
"current + N previous" key set for rotation is a later follow-up. No formal
timing claim is made beyond the constant-time MAC compare.

### `session-rotate!` — session-fixation defense

Rotate the session ID at a privilege boundary (e.g. right after a successful
login) without losing the data the user just keyed in:

```turmeric
(defn do-login [ctx : Ctx] : Response
  (let [uid (authenticate ctx)]
    (do
      (session-set! ctx "user_id" uid)
      (session-rotate! ctx)            ;; new ID, same data
      (redirect "/dashboard"))))
```

`session-rotate!` mints a fresh ID, keeps the live session map, and at flush
saves under the new ID, deletes the old store entry, and `Set-Cookie`s the new
ID. Calling it more than once in a request is safe (only the original entry is
deleted). With signing on, the cookie's MAC updates automatically.

### CSRF synchronizer tokens

`session/csrf` adds per-session CSRF tokens on top of the session machinery (no
new deps). Mint/read the token in a handler and guard state-changing requests
with the middleware:

```turmeric
(import tourist/middleware :refer [use!])
(import session/csrf       :refer [csrf-token csrf-mw default-csrf-opts])

(tourist 3000
  (session-mw (memory-store-new) (default-session-config))
  (use! (csrf-mw (default-csrf-opts)))     ;; 403s POST/PUT/PATCH/DELETE w/o a valid token
  (get!  "/form" (fn [ctx] (html (form-with-token (csrf-token ctx)))))
  (post! "/save" save-handler))
```

The token lives in the session under the reserved `__csrf` key. The middleware
accepts it as an `X-Csrf-Token` header (SPAs) or a configured form field
(classic forms — `(csrf-opts "field_name")`, default `csrf_token`); the compare
is constant-time. Safe methods (GET/HEAD/OPTIONS) always pass.

## Writing a custom store

The store is a small vtable, not a typeclass — tourist registers middleware as
a bare C-ABI function pointer with no captured environment, so the middleware
reads its store from a process-global, and a global cannot hold a value of an
erased typeclass-instance type. A backend therefore provides three functions
and an opaque state pointer and calls `store-new`:

```turmeric
;; load   : (c-fn [state:int id:cstr] int)            -> SessMap handle, 0 = miss
;; save   : (c-fn [state:int id:cstr map:int ttl:int] int)
;; delete : (c-fn [state:int id:cstr] int)
(store-new my-load my-save my-delete my-state)
```

Session data is a `SessMap` (see `session/data`) — a compact owned
string→string map. Stores clone on the way in and out so a request's working
copy never aliases the master copy. This is the same open-set extensibility a
typeclass would give, expressed in tourist's established fn-pointer idiom.

## Security notes

- **Session IDs** are 32 bytes (256 bits) from the OS CSPRNG
  (`getentropy`, falling back to `/dev/urandom`), hex-encoded. The ID is a
  bearer token; optionally HMAC-signed (see *Signed cookies*) but never
  encrypted (server-side storage means there is nothing to encrypt in the
  cookie).
- **Session fixation:** call `(session-rotate! ctx)` after a privilege change
  (e.g. login) to regenerate the ID while keeping the data.
- **CSRF:** add `(use! (csrf-mw (default-csrf-opts)))` to reject state-changing
  requests that lack a valid synchronizer token.
- The file store requires a POSIX filesystem (atomic `rename`). Session IDs
  are hex so they are always safe path components.

## Status

| Backend | State |
|---------|-------|
| memory store | ✅ implemented + tested |
| file store | ✅ implemented + tested |
| Valkey/Redis store | ✅ sibling spice [`tur-tourist-session-valkey`](../tourist-session-valkey) |

The Valkey store lives in a **sibling spice** so the `valkey`/hiredis native
dependency stays optional — apps using the memory or file store don't link it.
Its round-trip test is self-skipping when no Valkey server is reachable, so CI
stays green without one. All three backends sit behind one `Store` vtable, and
the JSON envelope is shared via `session/serde`.

## Tests

```sh
tur test spices/tourist-session/tests/session
```

Covers session ID generation, cookie parse/build, signed-cookie round-trip and
tamper rejection (HMAC against the RFC 4231 vector), the `SessMap`, the shared
JSON `serde` codec, both built-in stores through the `Store` vtable,
`session-rotate!`, the CSRF token + middleware decision, and the full ctx +
middleware request lifecycle (lazy load, dirty persist, destroy) driven against
fabricated tourist contexts.

## See also

- [Session guide](https://turmeric-lang.com/docs/html/guides/tourist-session-guide.html)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/tourist-session>
