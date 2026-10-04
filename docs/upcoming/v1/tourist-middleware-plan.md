# Plan: tourist middleware parity (the Express-shaped set)

> Status: proposed (2026-10-04). Nothing landed.
> Scope: `spices/httpd/` (server hardening), `spices/tourist/` (middleware
> and core helpers), one new spice `spices/tourist-compress/`. A small,
> optional parity section touches turmeric's `stdlib/httpd.tur`.
> Not on tur-signal's call surface, so it does not gate v1; phases are
> ordered by risk (security holes first), not by feature count.
> Measured against turmeric-spices `origin/main` 9d25587 and turmeric
> `origin/main` 7690314c1.

## Motivation

A user arriving from express.js expects a framework plus a shelf of
middleware: CORS, logging, error recovery, body parsing and limits, rate
limiting, auth, compression, security headers, request IDs, proxy awareness.
Turmeric has most of that shelf -- but on the **wrong server**.

There are two independent HTTP server stacks:

| Stack | Where | Middleware today |
|---|---|---|
| `stdlib/httpd.tur` | turmeric repo | `mw-cors`, `mw-log`, `mw-recover`, `mw-static` (ETag/304), `mw-json-body`, `mw-body-size`, `mw-rate-limit`, `mw-basic-auth`, gzip via `stdlib/httpd-compress.tur`; cookie get/set; form + multipart parsing; `httpd-req-remote-ip` |
| `spices/httpd` + `spices/tourist` | this repo | `use!` / `use-after!` hooks; sessions + CSRF (`tourist-session`); static files; templates; JSON codecs (`httpd/handler`) |

Tourist -- the Sinatra/Express-shaped framework a user would actually reach
for -- is built on `spices/httpd` (`tourist/app.tur:31`, `server-start` from
`httpd/server`), not on stdlib httpd. The stdlib `mw-*` functions wrap a
`(fn [ptr<void>] nil)` handler over stdlib's `HttpdConn`; tourist middleware
is `(fn [Ctx] (Option Response))` over a spice `Request`. The two cannot be
adapted into each other, so the tourist set has to be **ported**, not
wrapped. Apart from sessions/CSRF, a tourist app today has no CORS, no
logging, no recovery, no rate limiting, no auth, no compression, no
cookie helpers outside the session spice, and no form parsing.

Separately, a few things are missing from **both** stacks: security headers
(helmet), request IDs, `X-Forwarded-*` / trust-proxy handling, request
deadlines, and conditional GET for dynamic responses.

## Findings that shape the plan

1. **`spices/httpd` has no request-body cap.** `srv-recv-request`
   (`httpd/server.tur:168-239`) reads `Content-Length` and `realloc`-doubles
   its buffer until that many bytes arrive. Any client can make a worker
   allocate an arbitrary amount of memory. The header block is likewise
   unbounded (only the parsed header *count* is capped at 64,
   `httpd/parse.tur:124`).
2. **`spices/httpd` has no read timeout.** The only `setsockopt` in the
   httpd spice is `SO_REUSEADDR` (`server.tur:123`, `tls.tur:104`); a
   blocking `read` on a silent client never returns. With the default pool
   of 8 workers (`srv-default-pool-size`), eight idle connections wedge the
   server (slowloris). `ws-server` sets `SO_RCVTIMEO` after upgrade
   (`ws-server/server.tur:900`), so the pattern exists in-repo.
3. **A spice `Request` carries no peer address.** Rate limiting and
   logging need one; `getpeername` is never called.
4. **Tourist middleware is pre/post only.** `use!` runs before the route,
   `use-after!` after. There is no "around" form (Express's `next()`),
   so anything that must bracket the handler -- timing, recovery, a
   deadline -- needs two items sharing state through `ctx-attr-set!`.
5. **Whether `use!` closures may capture is contradictory in the docs.**
   `use!`'s docstring says the handler "may capture", and dispatch goes
   through `TUR_APPLY1` (fat-closure ABI, `tourist/dsl.tur:158`).
   `tourist-session/README.md` ("Writing a custom store") says tourist
   registers middleware "as a bare C-ABI function pointer with no captured
   environment", which is why `session-mw` keeps its config in a process
   global. Every configurable middleware below depends on which is true.
6. **Response bodies are `cstr`.** `with-body` / `response` take a `cstr`
   (`httpd/response.tur:123,199`). A gzip body contains NUL bytes, so
   compression needs a binary-safe body setter first.

Finding 1-3 are read from source, not yet reproduced; T1 starts by turning
each into a failing test.

## Non-goals

- HTTP/2, streaming request/response bodies, chunked uploads.
- Cancelling a running handler. Threads are not cancellable; a deadline
  can answer the client early but cannot stop the work (see T3).
- `method-override`, view engines beyond the existing template spice,
  WebSockets (`tourist-ws` exists).
- Unifying the two server stacks. That is a real question (see Open
  questions) but out of scope here.

## Decision: stdlib httpd stays a server users build on (2026-10-04)

The layering follows Node: `stdlib/httpd.tur` is the `http` module plus
connect-style middleware -- a supported base users build directly on --
and tourist is the express layer. Consequences for this plan:

- Middleware that belongs at the server level (security headers, request
  IDs, trust proxy) ships on **both** stacks. T5 is committed, not optional.
- Framework conveniences (routing-aware middleware, `Ctx` helpers,
  cookies-on-`Ctx`, multipart on `Ctx`) stay tourist-only.
- Behavior should match across the stacks: same option struct fields,
  same defaults, same header output, so moving an app from stdlib httpd
  to tourist does not change its responses.

## Design rules for every new API

- **No `:int` stand-ins** (turmeric CLAUDE.md). Middleware factories return
  `Item`; callbacks are spelled-out fn types; flags are `bool`; "maybe"
  values are `Option`/`Result`. Per-request values that are not integers
  (request ID, client IP) get typed accessors on `Ctx`, not
  `ctx-attr-set!` (whose value slot is `int`).
- **Configuration by `defstruct` options plus a `default-*-opts`**, the
  shape stdlib's `CorsOpts` already uses, and >5 params goes into a struct.
- **Safe defaults.** Nothing trusts proxy headers, enables credentials, or
  emits a CSP unless asked.
- **No process globals for middleware config** once T0 settles capture:
  globals make two differently configured instances in one process
  impossible.

## Phases

### T0 -- Settle the middleware calling convention

**Tasks**
- A tourist fixture that registers `use!` and `use-after!` closures that
  capture a `cstr` and a struct, under the pool server, and checks both
  values at request time across concurrent requests.
- If capture works: correct `tourist-session/README.md`, and optionally
  move `session-mw` off its global (separate change).
- If it does not: fix it in `tourist/middleware.tur` / `dsl.tur` (store
  the fat closure, not a bare pointer) before T2. Do not build T2 on
  globals.
- Add **`use-around!`**: `(fn [Ctx (fn [Ctx] Response)] Response)` -- the
  Express/Koa `next` shape. The framework passes a continuation that runs
  the rest of the chain and the route. Pre/post stay; around is what
  logging, recovery and deadlines want (finding 4).
- Measure what a panicking route handler does today (worker dies?
  process aborts? connection dropped?). This decides whether
  `recover` (T2) is a middleware or a framework option.
- Check that headers added with `ctx-add-header!` land on a
  **short-circuit** response from a `use!` item, not only on route
  responses. CORS and secure-headers rely on it.

**Acceptance**
- Capture fixture green; README statement matches behavior.
- `use-around!` fixture: two nested around items run in declaration order
  outside-in, and each sees the inner response.
- A short note in this plan recording the panic behavior and the
  short-circuit header result.

### T1 -- Harden `spices/httpd` (security; do first)

**Tasks**
- **`ServerOpts`** struct plus `server-start-opts` (and pool/conn
  variants), so limits don't grow positional params:
  `max-body-bytes` (default 1 MiB), `max-header-bytes` (default 64 KiB),
  `read-timeout-ms` (default 30000), `pool-size`.
- **Body cap:** reject before allocating when `Content-Length` exceeds
  `max-body-bytes` -> `413`. Reject a header block over `max-header-bytes`
  -> `431`. Stop reading on a missing or invalid `Content-Length` on a
  body-bearing method rather than reading to EOF.
- **Read timeout:** `SO_RCVTIMEO` on each accepted fd (plain and TLS
  paths); expiry before a full request -> `408` and close.
- **Peer address:** `getpeername` at accept; store on the request;
  `req-remote-addr : cstr` (IPv4 and IPv6).
- **Binary-safe body:** `with-body-bytes [resp bytes len]` and make
  `serialize-response` frame by stored length, not `strlen` (finding 6).
- **Header helpers:** `with-header-set` (case-insensitive replace) and
  `resp-header [resp name] : (Option cstr)` alongside the append-only
  `with-header` (see risk 5).
- Tourist: `tourist-opts [port opts & items]` forwarding `ServerOpts`.

**Acceptance**
- Tests: oversized `Content-Length` gets `413` with no large allocation;
  a client that sends half a header and stalls gets `408` within the
  timeout; nine stalled clients do not block a tenth real request;
  `req-remote-addr` returns `127.0.0.1` for a loopback client; a body
  containing NUL round-trips with the correct `Content-Length`.

### T2 -- Port the stdlib set to tourist

New modules under `spices/tourist/src/tourist/mw/` (no native deps, so
they ship with tourist; exported from `build.tur`). Each is one `Item`.

| Middleware | Kind | Notes |
|---|---|---|
| `cors` + `CorsOpts` | `use!` | Fields as stdlib `CorsOpts`, but `allow-origins` is a list with exact-match echo plus `Vary: Origin`; `allow-credentials : bool`; reject `"*"` + credentials at construction. Preflight (`OPTIONS` + `Access-Control-Request-Method`) short-circuits `204`; normal flow adds headers via `ctx-add-header!`. |
| `logger` + `LogOpts` | `use-around!` | One line per request: method, path, status, bytes, ms, request ID (T3). Sink is `(fn [cstr] unit)`, default stdout. Escape control bytes in method/path as stdlib `mw-log` does (log injection). |
| `recover` | `use-around!` or framework option | Per T0's panic finding. Returns `500` with a generic body; detail only to the log sink. |
| `rate-limit` + `RateLimitOpts` | `use!` | Fixed window per key, mutex-guarded (pool workers are threads). Key fn `(fn [Ctx] cstr)`, default client IP (T3 `req-ip`). `429` + `Retry-After` + `RateLimit-*` headers. Bounded table with eviction. |
| `basic-auth` | `use!` | Verifier `(fn [cstr cstr] bool)`; constant-time compare helper exported; `401` + `WWW-Authenticate: Basic realm=...`. |
| `body-limit` | `use!` | Per-route cap tighter than `ServerOpts.max-body-bytes` -> `413`. |
| form parsing | helper | `form-param [ctx key] : (Result cstr cstr)` for `application/x-www-form-urlencoded`, percent-decoding included. |
| cookies | helper | Generalize `tourist-session/src/session/cookie.tur` into tourist core: `req-cookie [ctx name] : (Option cstr)`, `set-cookie! [ctx CookieOpts]` (SameSite, HttpOnly, Secure, Max-Age, Path, Domain). `tourist-session` then depends on it instead of owning it. |

**Acceptance**
- One fixture per row under `spices/tourist/fixtures/`, each covering the
  happy path plus the rejection path (preflight, 401, 429, 413, malformed
  form, bad cookie syntax).
- `tourist-session` suite still green after the cookie move.
- README gains a "Middleware" section mapping each Express package to its
  tourist name.

### T3 -- What both stacks lack

Tourist first; stdlib parity is T5.

- **`secure-headers` + `SecureHeadersOpts`** (helmet). Defaults:
  `X-Content-Type-Options: nosniff`, `X-Frame-Options: DENY`,
  `Referrer-Policy: no-referrer`, `Cross-Origin-Opener-Policy: same-origin`.
  Opt-in: `Strict-Transport-Security` (only meaningful over TLS; HSTS on by
  accident locks a domain to https), and `Content-Security-Policy` as a
  caller-supplied string -- no default CSP, because a wrong default breaks
  apps silently.
- **Request ID.** Accept an incoming `X-Request-Id` only if it is 1-128
  chars of `[A-Za-z0-9._-]`; otherwise generate 128 random bits as hex.
  Stored on `Ctx`, read with `req-id [ctx] : cstr`, echoed as a response
  header, and included by `logger`.
- **Trust proxy.** `TrustProxyOpts` with a list of trusted CIDRs (default:
  none). `req-ip [ctx] : cstr` walks `X-Forwarded-For` right to left and
  returns the first hop not in a trusted range, falling back to
  `req-remote-addr`. `req-proto [ctx] : cstr` honors `X-Forwarded-Proto`
  only from a trusted peer. `rate-limit`, `logger`, HSTS and secure-cookie
  decisions use these, never the raw headers.
- **Conditional GET.** `etag` (`use-after!`): weak ETag from a hash of a
  `200` body on `GET`/`HEAD`; matching `If-None-Match` -> `304` with no body.
  Add strong ETag + `Last-Modified` + `If-Modified-Since` to
  `tourist/static.tur`, which emits neither today.
- **Request deadline** (lowest priority). `timeout [ms]` as
  `use-around!` that answers `503` when the handler overruns. The
  handler keeps running (non-goal: cancellation), and the doc must say so.
  The T1 read timeout covers the attack case; this one only covers slow
  handlers.

**Acceptance**
- Fixtures: default secure headers present on route and short-circuit
  responses; request ID echoed when valid, replaced when malicious
  (CRLF, 1 KiB); `X-Forwarded-For` ignored without a trust config and
  honored with one; `304` on a matching ETag for dynamic and static
  responses.

### T4 -- `tourist-compress` (new spice)

Separate spice so the zlib native dep stays optional, mirroring the
`tourist-session` / `tourist-session-valkey` split.

- `compress` + `CompressOpts` (`use-after!`): gzip when `Accept-Encoding`
  allows it, the body is over a threshold (default 1 KiB), and the
  content type is compressible (text/*, JSON, JS, SVG); skip when
  `Content-Encoding` is already set. Always add `Vary: Accept-Encoding`.
  Needs T1's binary body. Strip a strong ETag to weak (or drop it), since
  the bytes changed.

**Acceptance**
- Fixture decodes the gzip body back to the original; a small body and an
  image are passed through untouched; `Vary` is present in both cases.

### T5 -- stdlib httpd parity (turmeric repo)

Committed per the decision above. Port the T3 server-level items to
`stdlib/httpd.tur` in its existing `mw-*` style:

- `mw-secure-headers` + `SecureHeadersOpts` (same fields and defaults as
  tourist's).
- `mw-request-id` + `httpd-req-id`, same accept/generate rule.
- `TrustProxyOpts` + `httpd-req-ip` / `httpd-req-proto`; make
  `mw-rate-limit` and `mw-log` use `httpd-req-ip` instead of the raw peer
  address.
- `mw-etag` for dynamic responses (`mw-static` already does static).
- Check stdlib httpd for the T1 findings too (body cap exists via
  `httpd-set-max-body!`; confirm a read timeout exists) and fix any gap
  there in the same PR.

Separate turmeric PR with its own fixtures; can run in parallel with T2/T3
once T3's option structs are settled, so both stacks share one shape.

### T6 -- Multipart for tourist (largest; last)

`req-file [ctx field] : (Option Part)` with typed `Part` accessors, ported
from stdlib's parser (`httpd-req-multipart-parse`). Bounded by T1's body
cap; in-memory only (streaming is a non-goal). Fuzz the boundary parser.

## Ordering

```
T0 (convention) --+--> T2 (ports) --> T3 (new) --> T6 (multipart)
T1 (hardening) ---+--> T4 (compress, needs binary body)
                                       T5 (stdlib, after T3's option shapes)
```

T0 and T1 are independent and can run in parallel. T1 is the most
important item in this plan: findings 1 and 2 are remotely triggerable
denial of service on any tourist app, middleware or not.

## Risks and open questions

1. **Two server bases.** Decided: stdlib httpd stays user-facing (see
   Decision). Still open: in Node, express sits *on* `http`; here tourist
   sits on `spices/httpd`, a second server, not on stdlib httpd. Long term,
   should tourist move onto stdlib httpd (or the spice server fold into
   it) so there is one base, as in Node? Until then, server-level fixes
   such as T1 land twice. Not decided here.
2. **Panic recovery may not be expressible** (T0). If a panicking handler
   aborts the process, `recover` becomes a framework-level change
   (catch at the worker boundary), not a middleware.
3. **Rate-limit state across workers.** In-memory and per-process only.
   Multi-process deployments need a store (Valkey, as sessions do);
   leave a key/store seam but ship memory only.
4. **`ctx-add-header!` on short-circuit responses** (T0). If unsupported,
   CORS and secure-headers must decorate in a `use-after!` instead, and
   T0 fixes the gap rather than having every middleware work around it.
5. **Header duplicates.** `with-header` only appends
   (`httpd/response.tur:75`); there is no replace or lookup. Middleware
   that sets a header a route may already have set (secure-headers,
   `Content-Encoding`, ETag) needs `with-header-set` (case-insensitive
   replace) and `resp-header [resp name] : (Option cstr)`; `Vary` should
   merge tokens rather than repeat the header. Add these in T1.
