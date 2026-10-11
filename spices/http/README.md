# tur-http

HTTP/HTTPS client for Turmeric via mbedTLS: `http-get`, `http-post`, request
and response objects with status and body accessors.

## Overview

`tur-http` is a Tier 3 spice (`cmake-dep` -- pulls in `mbedTLS 3.6.2` via
`tur fetch`). It exposes a small synchronous HTTP/HTTPS client: build a
request, send it, and inspect status / headers / body on the response.

TLS is provided by the bundled mbedTLS so the spice does not depend on
system OpenSSL. Use it for REST API calls, health checks, and any client-side
HTTP integration that does not need full async or HTTP/2.

## Install

```turmeric no-check
:spices {
  "http" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "http-v0.1.0"
          :subdir "spices/http"}
}
```

## Quick start

```turmeric
(import http/client   :refer [http-get])
(import http/response :refer [response-status response-body])

(let [r (http-get "https://httpbin.org/get")]
  (when (ok? r)
    (let [resp (ok-val r)]
      (println (response-status resp))
      (println (response-body resp)))))
```

```sweet-exp
#lang sweet-exp
import http/client   :refer [http-get]
import http/response :refer [response-status response-body]

let [r http-get("https://httpbin.org/get")]
  when ok?(r)
    let [resp ok-val(r)]
      println $ response-status resp
      println $ response-body resp
```

## Freeing results and requests

Every request function returns a heap result box that the caller owns.
`(http-result-free r)` releases it along with what it carries: the response
(status, body, headers -- strings you read with `response-body` /
`response-header` go with it, so read first), or the error message.
`(request-free req)` releases a record built by `request` / `json-request`,
including the `Content-Type` cells `json-request` added; the rest of a header
list belongs to whoever built it.

```turmeric
(import http/client   :refer [http-get http-result-free])
(import http/response :refer [response-status])

(let [r      (http-get "http://127.0.0.1:8080/health")
      status (if (ok? r) (response-status (ok-val r)) 0)]
  (http-result-free r)
  status)
```

```sweet-exp
#lang sweet-exp
import http/client   :refer [http-get http-result-free]
import http/response :refer [response-status]

let [r      http-get("http://127.0.0.1:8080/health")
     status if(ok?(r) response-status(ok-val(r)) 0)]
  http-result-free(r)
  status
```

## Typed JSON bodies

`http/request` and `http/response` expose typed JSON codecs built on the json
spice's `EncodeJson`/`DecodeJson` typeclasses, so a request/response body is a typed
struct rather than a hand-walked document:

- `(json-request method url x headers) : int` -- encode `x` as the request
  body and prepend `Content-Type: application/json` to `headers`. Generic over
  any `EncodeJson` instance.
- `(response-decode resp T) : (Result T cstr)` -- decode the response body
  into `T` via its `DecodeJson` instance, or `err` on a non-JSON body. The typed
  counterpart to `response-json` (which returns an untyped doc handle).

```turmeric
(import http/client   :refer [http-request])
(import http/request  :refer [json-request])
(import http/response :refer [response-decode])

(defstruct User [id : int  name : cstr])
(derive-json User (id int) (name cstr))

(let [req (json-request "POST" "https://api.example.com/users"
                        (make-struct User 7 "ann") 0)
      r   (http-request req)]
  (when (ok? r)
    (let [u (response-decode (ok-val r) User)]
      (when (ok? u) (println (.name (ok-val u)))))))
```

These build on json's codec surface. `http` no longer re-declares `yyjson` in
its own `:cmake-deps`: the compiler collects `:cmake-deps` transitively across
the declared `:spices` (and workspace siblings), so `yyjson` is inherited from
the `json` dependency. (`http`'s `:cmake-deps` still pins `mbedTLS` directly --
that is its own TLS backend, not a transitive dep.) yyjson, which was
previously optional (only `response-json` used it, stubbing out when absent),
is now a first-class part of the typed codec path.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/http>
