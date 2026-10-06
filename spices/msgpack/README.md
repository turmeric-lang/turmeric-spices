# tur-msgpack

MessagePack binary serialization for Turmeric -- the binary twin of
[tur-json](../json/README.md).

Same typeclass-driven surface, same derive-macro family, same accumulate-all-
errors checked-decode layer. Where json traffics in malloc'd `cstr` fragments,
msgpack is a binary format with embedded NUL bytes, so the codec traffics in an
owned, length-prefixed byte buffer (`Buf`) instead.

**No native dependencies.** Both halves of the codec are vendored C in this
spice; nothing is fetched, and a consumer builds offline. See
[Why not mpack](#why-not-mpack) below.

## Quick start

```turmeric
(import msgpack/buf    :refer [Buf buf-free buf->hex])
(import msgpack/decode :refer [mp-parse mp-tree-root mp-tree-free])
(import msgpack/encode :refer [derive-msgpack])

(defstruct User [id : int  name : cstr  active : bool])
(derive-msgpack User (id int) (name cstr) (active bool))

(let [b (encode-mp (make-struct User 7 "alice" true))]   ;; owned Buf
  ;; 83 a2 69 64 07 a4 6e 61 6d 65 a5 61 6c 69 63 65 a6 61 63 74 69 76 65 c3
  ;;  = {"id":7,"name":"alice","active":true} as a fixmap
  (let [t (ok-val (mp-parse b))
        u (ok-val (:: (decode-mp t (mp-tree-root t)) (Result User cstr)))]
    (println (.name u))
    (mp-tree-free t))
  (buf-free b))
```

## The three classes

All three are **format-tagged**, and so are their methods. Typeclasses resolve
globally, so a program holding both this spice and tur-json cannot have two
classes named `Encode` -- and, worse, two classes declaring a method named
`encode` compile with **no diagnostic** and dispatch to whichever instance
registered last. Distinct method names are the only thing keeping the two
spices apart. (tur-json renamed its own classes in 0.4.0 for the same reason;
neither spice owns the unqualified spelling.)

| Class | Method | Direction |
| --- | --- | --- |
| `EncodeMp` | `encode-mp : a -> Buf` | value -> owned fragment |
| `DecodeMp` | `decode-mp : MpTree -> MpNode -> (Result a cstr)` | fail-fast read |
| `DecodeMpChecked` | `decode-mp-checked : MpTree -> MpNode -> (Result a MpDecodeErrors)` | accumulating validator |

`decode-mp` is **return-type-directed**: `a` appears only in the return type,
so the instance is selected by the ascription at the call site.

```turmeric
(:: (decode-mp t n) (Result int  cstr))   ;; picks DecodeMp [int]
(:: (decode-mp t n) (Result User cstr))   ;; picks the derived instance
```

Primitive instances ship for `int`, `bool`, `float`, `cstr`, `(Option A)` and
`(Cons A)`. A list decodes through the standalone `decode-mp-list` rather than
a `DecodeMp [Cons]` instance -- a `(Cons A)` shares the `:int` carrier with
`int`, which defeats the return-type dispatch (the same reason json has
`decode-json-list`).

## Derive macros

| Macro | For | Emits |
| --- | --- | --- |
| `derive-msgpack T (f t)...` | `defstruct` | both directions |
| `derive-msgpack-encode` / `-decode` | `defstruct` | one direction |
| `derive-msgpack-opaque T :as carrier` | `defopaque` | both, via the carrier's instances |
| `derive-msgpack-opaque-encode` / `-decode` | `defopaque` | one direction |
| `derive-msgpack-sum T (Ctor (f t)...)...` | `defdata` | both, externally tagged |
| `derive-msgpack-sum-encode` / `-decode` | `defdata` | one direction |
| `derive-mp-decoder T (f t)...` | `defstruct` | a `DecodeMpChecked` instance |

The field-type slot is parsed but unused on the encode side -- reserved for the
renaming / `:skip` policy both spices will eventually want, ideally upstream of
both.

## Wire shapes

- A **struct** is a `map` with string keys, in declaration order. Decoding looks
  each key up by name, so a producer that reorders the map still decodes.
- A **sum** is externally tagged: `{"Ctor": {...fields...}}`, a one-entry map.
  A nullary constructor gets an empty inner map. Same convention as json, so
  the two formats describe the same value the same way.
- An **opaque** goes on the wire as its carrier -- only where you asked for it
  with `derive-msgpack-opaque`.
- `none` and a missing key both read back as `none`; `none` encodes as `nil`.
- Integers take the narrowest format that fits (fixint through int64/uint64);
  floats are always `float64`, because narrowing a double to `float32` loses
  information.

## Validating untrusted input

`decode-mp` fails fast and, through the derive macros, takes `ok-val` of a
failed field -- so a missing key silently yields a garbage field. That is the
right trade for a trusted producer and the wrong one at a trust boundary. At a
boundary, reach for `derive-mp-decoder`:

```turmeric
(defstruct Person [name : cstr  age : int])
(derive-mp-decoder Person (name cstr) (age int))

(let [r (:: (decode-mp-checked t root) (Result Person MpDecodeErrors))]
  (if (ok? r)
    (use (ok-val r))
    (let [e (err-val r)]
      ;; every violation, not just the first
      (println (mp-decode-errors-count e))        ; => 2
      (println (mp-decode-error-path     e 0))    ; => "name"
      (println (mp-decode-error-expected e 0))    ; => "string"
      (println (mp-decode-error-got      e 0))    ; => "int"
      (mp-decode-errors-free e))))
```

The type vocabulary (`int` / `string` / `bool` / `float` / `null` / `array` /
`object`, plus `missing`) mirrors `stdlib/schema.tur`, and a msgpack `map` is
reported as `"object"` on purpose -- a violation on a given struct reads
identically whether it came through this spice or tur-json.

`mp-parse` is the other half of the boundary: it validates the **whole** buffer
before handing out a single node, so truncation, a container that promises more
elements than it carries, the reserved `0xc1` byte, and trailing bytes after a
complete value are all rejected at the parse rather than discovered later as a
bad field. Every accessor downstream is total.

## Ownership

- `encode-mp` returns a **fresh owned `Buf`**. Free it with `buf-free`, or hand
  ownership on.
- `buf-concat` **consumes both** operands.
- `mp-parse` copies the bytes, so the caller still owns the input `Buf` and may
  release it immediately. `mp-tree-free` releases the tree and every node into
  it.
- `mp-get-str` (and `DecodeMp [cstr]`) return a **malloc'd copy** that outlives
  `mp-tree-free`; the caller owns it.

## Limitations (v0)

- No streaming: whole-value encode and decode only.
- No `ext` types, including the timestamp extension.
- No `bin`: Turmeric `cstr` maps to msgpack `str`, and a `bin` is not a string.
  Raw bytes are a follow-up alongside a byte-slice story.
- No compact positional structs -- map-with-string-keys only.
- No non-string map keys on the derive path (int-keyed maps are reachable
  through the low-level node API).
- No field renaming or `:skip`.
- Decode-side struct width is capped at 5 fields by
  `__mp-decode-make-struct`'s hand-unroll; hand-write the instance above that.
- A msgpack `str` may legally contain an embedded NUL, which `cstr` cannot
  represent -- such a value truncates at the first NUL.

## Why not mpack

The plan for this spice named [ludocode/mpack](https://github.com/ludocode/mpack)
as the decode backend, with hand-rolled encoding. MP0 measured that and chose to
hand-roll the reader too:

1. **mpack ships no `CMakeLists.txt` at any tag** (checked through v1.1.1), so
   the plan's `:cmake-deps` recipe cannot work as written -- it would need a
   shim project like the ones under `spices/raygui/cmake-deps/`.
2. The encoder was hand-rolled by design, so mpack would have been a native
   dependency for **one half** of the codec.
3. mpack's compile-time configuration lives in headers, and turmeric compiles
   emitted C with a bare `cc` that does not inherit a CMake target's compile
   definitions -- a real config-skew hazard (`MPACK_DEBUG`, `MPACK_STDIO`)
   between the archive and every consumer translation unit.
4. Zero dependencies means the spice builds offline on every platform CI covers.

This is the fallback the plan itself authorized. The reader and writer live in
`c/msgpack/mp_core.c` (~600 lines), reached through `:c-sources`, and the
Turmeric API is unchanged from the plan -- so mpack could still be swapped in
behind `mp-parse` / `mp-map-get` if it ever earns its way back.

Both halves are checked against a second implementation: the golden bytes in
`tests/` were cross-generated with CPython's `msgpack` at fixture-authoring
time, and the reader was swept under ASan/UBSan across every single-byte
mutation and every truncation of a corpus of documents, plus 200k random
inputs.

## Tests

```sh
cd spices/msgpack
tur fetch      # only for the optional json dep the cross-check test uses
tur test tests
```

`tests/json-cross-check.tur` is the one test that reaches outside this spice: it
derives **both** codecs for one struct in one program, which is exactly the
program that could not exist while tur-json owned the bare `Encode` / `Decode`.
tur-json is declared `:optional`, so a consumer of tur-msgpack never pulls it --
nor yyjson behind it.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/msgpack-guide.html)
- [tur-json](../json/README.md) -- the text twin; when to pick which is in the
  [developing-spices guide](https://github.com/turmeric-lang/turmeric/blob/main/docs/guides/developing-spices-guide.md)
- `stdlib/serial.tur` -- `Buf`'s layout peer (`{ int64 len; uint8 data[] }`)
