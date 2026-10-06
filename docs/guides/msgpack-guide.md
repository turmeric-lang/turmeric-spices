---
title: MessagePack Binary Serialization
category: Serialization
description: Typeclass-driven binary serialization -- encode/decode/derive macros for structs, opaques, and sums, with an accumulating validator for untrusted input
audience: developers serializing Turmeric values to a compact binary format, or interoperating with msgpack consumers
since: msgpack v0.1.0
---

# tur-msgpack Guide

MessagePack is a binary format that does the same job as JSON -- serialize a
structured value to bytes and back -- but in roughly half the space, with
embedded NUL bytes, and without the parsing cost of text. This spice is the
binary twin of `tur-json`: same typeclass-driven surface, same derive-macro
family, same accumulate-all-errors checked-decode layer. Where json
traffics in malloc'd `cstr` fragments, msgpack traffics in an owned,
length-prefixed byte buffer (`Buf`).

This guide walks the five things you will do most often:

1. [Encoding and decoding primitives](#1-primitives)
2. [Deriving codecs for structs](#2-structs)
3. [Deriving codecs for sums and opaques](#3-sums-opaques)
4. [Validating untrusted input](#4-validation)
5. [The low-level node tree](#5-node-tree)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "msgpack" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "msgpack-v0.1.0"
              :subdir "spices/msgpack"}
}
```

Then `tur fetch`. No native dependencies -- both halves of the codec are
vendored C in this spice; nothing is fetched, and a consumer builds offline.

---

## The one idea

Three typeclasses, each format-tagged so they can coexist with `tur-json`
in one program:

| Class | Method | Direction |
|-------|--------|-----------|
| `EncodeMp` | `encode-mp : a -> Buf` | value to owned fragment |
| `DecodeMp` | `decode-mp : MpTree -> MpNode -> (Result a cstr)` | fail-fast read |
| `DecodeMpChecked` | `decode-mp-checked : MpTree -> MpNode -> (Result a MpDecodeErrors)` | accumulating validator |

`decode-mp` is **return-type-directed**: `a` appears only in the return
type, so the instance is selected by the ascription at the call site:

```turmeric no-check
(:: (decode-mp t n) (Result int  cstr))   ;; picks DecodeMp [int]
(:: (decode-mp t n) (Result User cstr))   ;; picks the derived instance
```

---

## 1. Primitives

`encode-mp` takes any value with an `EncodeMp` instance and returns a
fresh owned `Buf`. `buf->hex` renders it for inspection; `buf-free`
releases it:

```turmeric
(defmodule demo
  (import msgpack/buf    :refer [Buf buf-free buf->hex])
  (import msgpack/encode :refer [encode-mp]))

  (defn main [] : int
    (let [b (encode-mp 42)]
      (println (buf->hex b))             ;; "2a"
      (buf-free b))
    (let [b (encode-mp true)]
      (println (buf->hex b))             ;; "c3"
      (buf-free b))
    (let [b (encode-mp "hi")]
      (println (buf->hex b))             ;; "a26869"
      (buf-free b))
    0))
```

```sweet-exp
defmodule demo
  import msgpack/buf    :refer [Buf buf-free buf->hex]
  import msgpack/encode :refer [encode-mp]

  defn main [] : int
    let [b encode-mp(42)]
      println $ buf->hex b             ;; "2a"
      buf-free(b)
    let [b encode-mp(true)]
      println $ buf->hex b             ;; "c3"
      buf-free(b)
    let [b encode-mp("hi")]
      println $ buf->hex b             ;; "a26869"
      buf-free(b)
    0
```

Primitive instances ship for `int`, `bool`, `float`, `cstr`, `(Option A)`,
and `(Cons A)`. Integers take the narrowest format that fits; floats are
always `float64` because narrowing a double to `float32` loses information.

---

## 2. Structs

`derive-msgpack` emits both `EncodeMp` and `DecodeMp` instances for a
`defstruct`. The struct encodes as a msgpack map with string keys, in
declaration order. Decoding looks each key up by name, so a producer that
reorders the map still decodes:

```turmeric
(defmodule demo
  (import msgpack/buf    :refer [buf-free buf->hex])
  (import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free])
  (import msgpack/encode :refer [derive-msgpack encode-mp decode-mp])

  (defstruct User [id : int  name : cstr  active : bool])
  (derive-msgpack User (id int) (name cstr) (active bool))

  (defn main [] : int
    (let [b (encode-mp (make-struct User 7 "alice" true))]
      (println (buf->hex b))
      ;; 83 a2 69 64 07 a4 6e 61 6d 65 a5 61 6c 69 63 65 a6 61 63 74 69 76 65 c3
      (let [t (ok-val (mp-parse b))
            u (ok-val (:: (decode-mp t (mp-tree-root t)) (Result User cstr)))]
        (println (.name u))             ;; "alice"
        (mp-tree-free t))
      (buf-free b))
    0))
```

```sweet-exp
defmodule demo
  import msgpack/buf    :refer [buf-free buf->hex]
  import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free]
  import msgpack/encode :refer [derive-msgpack encode-mp decode-mp]

  defstruct User [id : int  name : cstr  active : bool]
  derive-msgpack User (id int) (name cstr) (active bool)

  defn main [] : int
    let [b encode-mp(make-struct(User 7 "alice" true))]
      println $ buf->hex b
      ;; 83 a2 69 64 07 a4 6e 61 6d 65 a5 61 6c 69 63 65 a6 61 63 74 69 76 65 c3
      let [t ok-val(mp-parse(b))
            u ok-val(:: (decode-mp(t mp-tree-root(t))) (Result User cstr))]
        println $ .name u             ;; "alice"
        mp-tree-free(t)
      buf-free(b)
    0
```

### One direction at a time

`derive-msgpack-encode` and `derive-msgpack-decode` emit only one half.
Useful for telemetry frames (encode-only) or inbound config blobs
(decode-only):

```turmeric no-check
(defstruct LogEntry [level : cstr  msg : cstr])
(derive-msgpack-encode LogEntry (level cstr) (msg cstr))

(defstruct Config [port : int  host : cstr])
(derive-msgpack-decode Config (port int) (host cstr))
```

### Wire shapes

- A **struct** is a map with string keys, in declaration order.
- `none` and a missing key both read back as `none`; `none` encodes as `nil`.
- A list `(Cons A)` encodes as a msgpack array.

---

## 3. Sums and opaques

### Sum types

`derive-msgpack-sum` emits both directions for a `defdata` sum type,
externally tagged as a one-entry map `{"Ctor": {...fields...}}` -- the same
convention json uses, so the two formats describe the same value the same
way:

```turmeric
(defmodule demo
  (import msgpack/encode :refer [derive-msgpack-sum encode-mp decode-mp])
  (import msgpack/buf    :refer [buf->hex buf-free])
  (import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free])

  (defdata Event :copy [] (Click :int :int) (Scroll :int))
  (derive-msgpack-sum Event
    (Click (x int) (y int))
    (Scroll (dy int)))

  (defn main [] : int
    (let [b (encode-mp (Click 3 4))]
      (println (buf->hex b))
      ;; 81 a5 43 6c 69 63 6b 82 a1 78 03 a1 79 04
      (let [t (ok-val (mp-parse b))
            e (ok-val (:: (decode-mp t (mp-tree-root t)) (Result Event cstr)))]
        (mp-tree-free t))
      (buf-free b))
    0))
```

```sweet-exp
defmodule demo
  import msgpack/encode :refer [derive-msgpack-sum encode-mp decode-mp]
  import msgpack/buf    :refer [buf->hex buf-free]
  import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free]

  defdata Event :copy [] (Click :int :int) (Scroll :int)
  derive-msgpack-sum Event
    (Click (x int) (y int))
    (Scroll (dy int))

  defn main [] : int
    let [b encode-mp(Click(3 4))]
      println $ buf->hex b
      ;; 81 a5 43 6c 69 63 6b 82 a1 78 03 a1 79 04
      let [t ok-val(mp-parse(b))
            e ok-val(:: (decode-mp(t mp-tree-root(t))) (Result Event cstr))]
        mp-tree-free(t)
      buf-free(b)
    0
```

A nullary constructor encodes as `{"Ctor":{}}` (an empty inner map).

### Opaque newtypes

`derive-msgpack-opaque` emits both directions for a `defopaque` newtype by
delegating to its carrier's instances. The wire form is the carrier --
only where you asked for it:

```turmeric no-check
(defopaque UserId :int)
(derive-msgpack-opaque UserId :as int)
;; (encode-mp (:: 42 UserId))  =>  0x2a
;; (:: (decode-mp t n) (Result UserId cstr))  =>  parses an int as UserId
```

---

## 4. Validation

`decode-mp` fails fast and, through the derive macros, takes `ok-val` of a
failed field -- so a missing key silently yields a garbage field. That is
the right trade for a trusted producer and the wrong one at a trust
boundary. At a boundary, reach for `derive-mp-decoder`:

```turmeric
(defmodule demo
  (import msgpack/buf    :refer [buf-free])
  (import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free])
  (import msgpack/encode :refer [derive-mp-decoder decode-mp-checked
                                mp-decode-errors-count mp-decode-error-path
                                mp-decode-error-expected mp-decode-error-got
                                mp-decode-errors-free])

  (defstruct Person [name : cstr  age : int])
  (derive-mp-decoder Person (name cstr) (age int))

  (defn main [] : int
    ;; Suppose the input has name=123 (wrong type) and age is missing.
    (let [r (:: (decode-mp-checked t root) (Result Person MpDecodeErrors))]
      (if (ok? r)
        (use (ok-val r))
        (let [e (err-val r)]
          ;; every violation, not just the first
          (println (mp-decode-errors-count e))        ;; 2
          (println (mp-decode-error-path     e 0))    ;; "name"
          (println (mp-decode-error-expected e 0))    ;; "string"
          (println (mp-decode-error-got      e 0))    ;; "int"
          (mp-decode-errors-free e))))
    0))
```

```sweet-exp
defmodule demo
  import msgpack/buf    :refer [buf-free]
  import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free]
  import msgpack/encode :refer [derive-mp-decoder decode-mp-checked
                                mp-decode-errors-count mp-decode-error-path
                                mp-decode-error-expected mp-decode-error-got
                                mp-decode-errors-free]

  defstruct Person [name : cstr  age : int]
  derive-mp-decoder Person (name cstr) (age int)

  defn main [] : int
    ;; Suppose the input has name=123 (wrong type) and age is missing.
    let [r :: (decode-mp-checked(t root)) (Result Person MpDecodeErrors)]
      if ok?(r)
        use $ ok-val r
        let [e err-val r]
          ;; every violation, not just the first
          println $ mp-decode-errors-count e        ;; 2
          println $ mp-decode-error-path(e 0)      ;; "name"
          println $ mp-decode-error-expected(e 0)  ;; "string"
          println $ mp-decode-error-got(e 0)       ;; "int"
          mp-decode-errors-free(e)
    0
```

The type vocabulary (`int` / `string` / `bool` / `float` / `null` / `array`
/ `object`, plus `missing`) mirrors `stdlib/schema.tur`, and a msgpack
`map` is reported as `"object"` on purpose -- a violation on a given struct
reads identically whether it came through this spice or tur-json.

`mp-parse` is the other half of the boundary: it validates the **whole**
buffer before handing out a single node, so truncation, a container that
promises more elements than it carries, the reserved `0xc1` byte, and
trailing bytes after a complete value are all rejected at the parse rather
than discovered later as a bad field. Every accessor downstream is total.

---

## 5. Node tree

Under the derive macros, `mp-parse` turns a `Buf` into an owned `MpTree`,
and `mp-tree-root` gives you the root `MpNode`. You can walk the tree
directly with `mp-map-get`, `mp-arr-get`, `mp-arr-size`, `mp-map-size`, and
the typed reads `mp-get-int`, `mp-get-str`, `mp-get-bool`, `mp-get-float`:

```turmeric
(defmodule demo
  (import msgpack/buf    :refer [hex->buf buf-free])
  (import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free
                               mp-map-get mp-type-name mp-get-int])

  (defn main [] : int
    (let [b (hex->buf "81a16101")]       ;; {"a": 1}
      (let [t (ok-val (mp-parse b))]
        (let [root (mp-tree-root t)]
          (println (mp-type-name t root))  ;; "object"
          (let [v (unwrap (mp-map-get t root "a"))]
            (println (ok-val (mp-get-int t v)))))  ;; 1
        (mp-tree-free t))
      (buf-free b))
    0))
```

```sweet-exp
defmodule demo
  import msgpack/buf    :refer [hex->buf buf-free]
  import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free
                               mp-map-get mp-type-name mp-get-int]

  defn main [] : int
    let [b hex->buf("81a16101")]       ;; {"a": 1}
      let [t ok-val(mp-parse(b))]
        let [root mp-tree-root(t)]
          println $ mp-type-name(t root)  ;; "object"
          let [v unwrap(mp-map-get(t root "a"))]
            println $ ok-val $ mp-get-int(t v)  ;; 1
        mp-tree-free(t)
      buf-free(b)
    0
```

`MpTree` and `MpNode` are real opaques: nothing else in the program can be
passed where one is expected, and a tree cannot be used as a node. Every
accessor is total -- a wrong-typed or absent node yields `err` / `none` /
`"missing"`, never a read past the end.

---

## Ownership

- `encode-mp` returns a **fresh owned `Buf`**. Free it with `buf-free`, or
  hand ownership on.
- `buf-concat` **consumes both** operands.
- `mp-parse` copies the bytes, so the caller still owns the input `Buf` and
  may release it immediately. `mp-tree-free` releases the tree and every
  node into it.
- `mp-get-str` (and `DecodeMp [cstr]`) return a **malloc'd copy** that
  outlives `mp-tree-free`; the caller owns it.

---

## When not to use this

- **You need streaming.** v0 is whole-value encode and decode only.
- **You need `ext` types, including timestamps.** Not implemented.
- **You need `bin` type.** Turmeric `cstr` maps to msgpack `str`, and a
  `bin` is not a string. Raw bytes are a follow-up alongside a byte-slice
  story.
- **You need compact positional structs.** Map-with-string-keys only.
- **Your struct has more than 5 fields on the decode side.** The derive
  macro's `make-struct` call is hand-unrolled for 1-5 fields. Hand-write
  the `DecodeMp` instance above that.

---

## Tests

```sh
cd spices/msgpack
tur fetch      # only for the optional json dep the cross-check test uses
tur test tests
```

`tests/json-cross-check.tur` derives **both** codecs for one struct in one
program -- exactly the program that could not exist while tur-json owned
the bare `Encode` / `Decode` class names.

---

## See also

- [API reference](api/)
- [README](../../spices/msgpack/README.md) -- the full derive-macro table, wire shapes, and the "why not mpack" rationale
- [tur-nng guide](nng-guide.html) -- the messaging spice whose Payload layout matches Buf
- [tur-json](https://turmeric-lang.com/docs/html/guides/json-guide.html) -- the text twin; when to pick which
