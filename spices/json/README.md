# tur-json

JSON parsing and serialization for Turmeric via yyjson: parse, emit, and
in-place patch (`get-in` / `set-in` / `merge`).

## Overview

`tur-json` is a Tier 3 spice (`cmake-dep` -- pulls in `yyjson 0.10.0` via
`tur fetch`). It wraps yyjson's fast SAX/DOM parser behind a small Turmeric
surface: `json-parse` returns a `result<doc>`, `json-emit` /
`json-emit-pretty` serialize back to text, and the `json/patch` module
exposes dotted-path read/write helpers.

Use it for application config, REST payloads, and any JSON I/O where you
want yyjson's speed without dropping to inline C.

## Install

```turmeric no-check
:spices {
  "json" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "json-v0.1.0"
          :subdir "spices/json"}
}
```

## Quick start

```turmeric
(import json/parse :refer [json-parse json-free])
(import json/emit  :refer [json-emit-pretty])
(import json/patch :refer [json-get-in])

(let [r (json-parse "{\"user\":{\"name\":\"Alice\",\"age\":30}}")]
  (when (ok? r)
    (let [doc (ok-val r)]
      (println (json-get-in doc "user.name"))
      (println (json-emit-pretty doc))
      (json-free doc))))
```

```sweet-exp
#lang sweet-exp
import json/parse :refer [json-parse json-free]
import json/emit  :refer [json-emit-pretty]
import json/patch :refer [json-get-in]

let [r json-parse("{\"user\":{\"name\":\"Alice\",\"age\":30}}")]
  when ok?(r)
    let [doc ok-val(r)]
      println $ json-get-in doc "user.name"
      println $ json-emit-pretty doc
      json-free(doc)
```

## Typed encoding (`json/encode`)

The `EncodeJson` typeclass + `derive-json` macro give defstruct types a
JSON serializer without hand-writing per-type code. Primitive instances
ship for `int`, `bool`, `cstr`, and `float` (mirroring the `DecodeJson`
primitives, so floats round-trip); the macro emits one for any
`defstruct` product type whose fields you list explicitly:

```turmeric
(import json/encode :refer [derive-json])

(defstruct User [id : int  name : cstr  active : bool])
(derive-json User (id int) (name cstr) (active bool))

(println (encode-json (make-struct User 7 "alice" true)))
;; => {"id":7,"name":"alice","active":true}
```

This is the **P2a minimal slice** of the spices type-features uplift
plan (`docs/upcoming/spices-type-features-uplift-plan.md` in the
turmeric repo). It deliberately does not yet ship:

- `DecodeJson` (read-side roundtrip).
- `derive-json` for `defdata` sum types.
- `:as :carrier` opt-in for `defopaque` wire form.
- `:rename-fields` / `:only` / `:skip` codec options.

The earlier 2-field cap (from a closure-codegen bug in the main
turmeric compiler) was lifted 2026-06-12 in the same session that
introduced this module; arbitrary field counts work today.

## Typed decoding (`json/decode` + `DecodeJson` typeclass)

A self-contained tiny scanner (no yyjson dependency in this slice)
parses a JSON cstr into an opaque doc handle; the `DecodeJson` typeclass
on top dispatches per-type via a return-type ascription:

```turmeric
(import json/decode :refer [json-parse-doc json-doc-free json-doc-root json-obj-get])
(import json/encode)  ;; DecodeJson class lives here alongside derive-json

(let [doc      (unsafe (json-parse-doc "{\"id\":42,\"name\":\"alice\"}"))
      root     (unsafe (json-doc-root doc))
      id-r     (:: (decode-json doc (unsafe (json-obj-get doc root "id")))   (Result int  cstr))
      name-r   (:: (decode-json doc (unsafe (json-obj-get doc root "name"))) (Result cstr cstr))]
  (println (ok-val name-r))
  (unsafe (json-doc-free doc)))
```

Each call to `(decode-json doc val)` returns `(Result T cstr)` where `T` is
pinned by the ascription; on success the Result is `(ok x)`, on a
type/format error it carries a short static err message. `DecodeJson`
instances ship for `int` and `cstr` (with `true`/`false` decoding
through `DecodeJson [int]` as `1`/`0`).

Scope (intentionally narrow for the minimal slice):

- Flat objects only -- no nested objects or arrays.
- Integers only -- no floats.
- Strings handle the minimal escape set (`\"`, `\\`, `\b`, `\f`,
  `\n`, `\r`, `\t`, `\/`); `\uXXXX` is rejected.

`derive-json` emits the EncodeJson side for any defstruct. The matching
`DecodeJson [T]` side for a user-defined defstruct is hand-written today
(inline-C that allocates the struct, fills it from the per-field
DecodeJson dispatches, and wraps via `tur_ok`) -- the macro-driven DecodeJson
side lands once the value-struct-to-carrier boxing helper is in
place.

## See also

- [Guide](https://turmeric-lang.com/docs/html/guides/json-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/json>
