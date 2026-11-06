# tur-tls

TLS termination for Turmeric over [mbedTLS](https://tls.mbed.org/). The
v0.1.0 line is server-side only; client TLS is a future spice.

## Overview

`tur-tls` is a `cmake-dep` spice that wraps mbedTLS. It exposes four
small Turmeric modules:

- `tls/ctx` -- server-side `SSL_CTX` equivalent: load PEM certs/keys.
- `tls/conn` -- per-connection state: wrap an accepted fd, drive the
  handshake, then read/write encrypted bytes.
- `tls/autolink` -- internal: contributes `-lmbedtls -lmbedx509
  -lmbedcrypto` to the final link line via `__tur_autolink__`.
- `tls/httpd` -- bridge between this spice and `stdlib/httpd.tur`'s H5
  TLS hook table. Calling `tls-httpd-init` once flips an httpd server
  from `httpd-new` to `httpd-new-tls`.

The primary consumer is [`stdlib/httpd`](https://github.com/turmeric-lang/turmeric/blob/main/stdlib/httpd.tur)
(milestone H5), but `tls/conn` is general enough to layer over any
byte-stream socket -- SMTP, IMAP, custom protocols, etc.

`tur-tls` is intentionally **not** in the stdlib: a default `tur` install
should not require a TLS library. Importing the spice is the user's
opt-in.

## Status

| Module        | Status            |
|---------------|-------------------|
| `tls/autolink`| Shipped (v0.1.0)  |
| `tls/ctx`     | Shipped (v0.1.0, T2) |
| `tls/conn`    | Shipped (v0.1.0, T3) |
| `tls/httpd`   | Shipped (v0.1.0, T5) |

See [`docs/tur-tls-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/tur-tls-plan.md)
for the full roadmap. For the httpd integration story, see
[`docs/guides/httpd-tls-guide.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/guides/httpd-tls-guide.md).

## Install

### Native dependency

The spice's `:cmake-deps` block fetches and statically links mbedTLS
v3.6.2 via CMake `FetchContent` on first `tur fetch`. No system
package install is required; the build is self-contained.

Initial fetch takes ~1-2 minutes (download + build mbedTLS). Subsequent
builds reuse the cached build artefacts in `cmake/build/_deps/`.

mbedTLS 2.x is **not supported** -- its header layout differs from 3.x,
and the pinned `:ref` is `v3.6.2` (the current LTS line).

### Spice declaration

Add to your project's `build.tur`:

```turmeric no-check
:spices #{
  "tls" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
         :ref    "tls-v0.1.0"
         :subdir "spices/tls"}
}
```

Then run `tur fetch` to resolve `:cmake-deps`.

## Generating a server certificate

For local dev + CI smoke tests, the spice ships
[`tools/gen-cert.sh`](tools/gen-cert.sh) which emits a self-signed
1-day cert into a target directory:

```sh
tools/gen-cert.sh /tmp/tls-smoke-test
# Writes:
#   /tmp/tls-smoke-test/test-cert.pem
#   /tmp/tls-smoke-test/test-key.pem
```

The script wraps a single OpenSSL invocation:

```sh
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout key.pem -out cert.pem \
    -days 1 -subj "/CN=localhost"
```

The cert is **throwaway**: 1-day validity, CN=localhost, 2048-bit RSA,
unencrypted key (the spice does not implement password-protected key
loading in v0.1.0). Do not use it for anything real.

For production, use whatever PEM cert + key your CA issues -- the
spice's `tls-ctx-load-cert-pem` / `tls-ctx-load-key-pem` accept any
PEM-encoded file mbedTLS itself accepts.

## Quick start (raw TLS, no httpd)

```turmeric
(import tls/ctx  :refer [tls-ctx-new tls-ctx-load-cert-pem tls-ctx-load-key-pem])
(import tls/conn :refer [tls-wrap-fd tls-handshake tls-read tls-write tls-free])

(let [ctx (tls-ctx-new)]
  (tls-ctx-load-cert-pem ctx "/etc/letsencrypt/live/example.com/fullchain.pem")
  (tls-ctx-load-key-pem  ctx "/etc/letsencrypt/live/example.com/privkey.pem")
  ;; For each accepted TCP fd:
  ;;   (let [conn (tls-wrap-fd ctx fd)]
  ;;     (tls-handshake conn)
  ;;     ;; encrypted I/O via tls-read / tls-write on conn
  ;;     (tls-shutdown conn)
  ;;     (tls-free conn))
  )
```

```sweet-exp
#lang sweet-exp
(import tls/ctx  :refer [tls-ctx-new tls-ctx-load-cert-pem tls-ctx-load-key-pem])
(import tls/conn :refer [tls-wrap-fd tls-handshake tls-read tls-write tls-free])

let [ctx tls-ctx-new()]
  tls-ctx-load-cert-pem(ctx "/etc/letsencrypt/live/example.com/fullchain.pem")
  tls-ctx-load-key-pem(ctx  "/etc/letsencrypt/live/example.com/privkey.pem")
  ;; For each accepted TCP fd:
  ;;   let [conn tls-wrap-fd(ctx fd)]
  ;;     tls-handshake(conn)
  ;;     ;; encrypted I/O via tls-read / tls-write on conn
  ;;     tls-shutdown(conn)
  ;;     tls-free(conn)
```

### Linear `TlsConn` (U1)

`TlsConn` is a `:linear` opaque. A conn returned by `tls-wrap-fd` must be
released exactly once with `tls-free` (its single consumption); the
observing operations -- `tls-handshake`, `tls-read`, `tls-write`,
`tls-shutdown`, `tls-conn-null?` -- take it by `^borrow`, so they read it
without discharging that obligation. Under `-Xsubstructural` this turns
use-after-free (`tls-free` then `tls-read`) and forgetting to free into
compile-time errors (`TUR-E0101` / `TUR-E0100`) instead of runtime faults.
The discipline is inert in ordinary builds, so existing call sites compile
unchanged. See `tests/conn_linear_test.tur` for the borrow/consume idiom
and `errors/` for the rejected cases.

## Quick start (HTTPS via stdlib/httpd)

The H5 milestone of stdlib/httpd makes the integration one-line:

```turmeric
(load "stdlib/httpd.tur")
(import tls/ctx   :refer [tls-ctx-new tls-ctx-free
                           tls-ctx-load-cert-pem tls-ctx-load-key-pem])
(import tls/httpd :refer [tls-httpd-init])

(defn main [] : int
  (tls-httpd-init)            ;; wire spice -> httpd hook table (once)
  (let [ctx (tls-ctx-new)]
    (tls-ctx-load-cert-pem ctx "/tmp/tls-smoke-test/test-cert.pem")
    (tls-ctx-load-key-pem  ctx "/tmp/tls-smoke-test/test-key.pem")
    (let [h (httpd-new-tls 8443 4 my-handler ctx)]
      (httpd-run h)
      (httpd-free h)
      (tls-ctx-free ctx)
      0)))
```

```sweet-exp
#lang sweet-exp
load("stdlib/httpd.tur")
(import tls/ctx   :refer [tls-ctx-new tls-ctx-free
                           tls-ctx-load-cert-pem tls-ctx-load-key-pem])
(import tls/httpd :refer [tls-httpd-init])

defn main [] : int
  tls-httpd-init()            ;; wire spice -> httpd hook table (once)
  let [ctx tls-ctx-new()]
    tls-ctx-load-cert-pem(ctx "/tmp/tls-smoke-test/test-cert.pem")
    tls-ctx-load-key-pem(ctx  "/tmp/tls-smoke-test/test-key.pem")
    let [h httpd-new-tls(8443 4 my-handler ctx)]
      httpd-run(h)
      httpd-free(h)
      tls-ctx-free(ctx)
      0
```

See the full guide at
[`docs/guides/httpd-tls-guide.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/guides/httpd-tls-guide.md)
for migration steps, what happens under the hood, and how to verify a
running server with `curl` / `openssl s_client`.

## Non-goals (v0.1.0)

- Client-side TLS
- ACME / cert rotation / OCSP stapling
- HTTP/2 or ALPN selection
- SNI (one cert per context)
- mTLS / client certificate verification
- Encrypted PEM private keys

These are explicitly punted to follow-ups so the v1 API stays small.

## See also

- [tur-tls plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/tur-tls-plan.md)
- [tur-httpd plan](https://github.com/turmeric-lang/turmeric/blob/main/docs/tur-httpd-plan.md) (H5 = tls integration)
- [httpd-tls integration guide](https://github.com/turmeric-lang/turmeric/blob/main/docs/guides/httpd-tls-guide.md)
- mbedTLS: <https://tls.mbed.org/>
