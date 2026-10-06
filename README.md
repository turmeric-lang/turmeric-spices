# turmeric-spices

[![CI](https://github.com/turmeric-lang/turmeric-spices/actions/workflows/ci.yml/badge.svg)](https://github.com/turmeric-lang/turmeric-spices/actions/workflows/ci.yml)

Official monorepo of first-party spices for the [Turmeric](https://github.com/turmeric-lang/turmeric) ecosystem. The badge above is aggregate across **all** spices on both Linux and macOS — it turns red if any matrix leg fails.

**Canonical repo:** https://github.com/turmeric-lang/turmeric-spices

---

## Spices

Tiers: **1** = pure Turmeric, **2** = inline-C, **3** = cmake-dep

Platforms: ✅ works · ⚠️ partial, see the notes below · ❌ not usable · ? unaudited

**macOS and Linux are measured.** CI builds and runs the test suite for every
spice on both legs of every push, and the badge above is that run.
**Windows (MSYS2/UCRT64) and WASM (Emscripten) have no CI leg**, so those two
columns record what each spice's own C surface and its upstream dependency
support -- a derived read, not a tested result. The WASM column follows
turmeric's [Emscripten spice audit](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/hold/wasm-spices-plan.md#9-per-spice-emscripten-compatibility-matrix).

| Spice | Description | Tier | macOS | Linux | Windows | WASM | C dep |
|-------|-------------|------|-------|-------|---------|------|-------|
| [`tur-test`](spices/test/) | Testing framework utilities | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-math`](spices/math/) | 2D/3D vector and matrix math | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-c-dsl`](spices/c-dsl/) | Lisp-syntax DSL that compiles to C99 source | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-glsl`](spices/glsl/) | Lisp-syntax DSL that compiles to GLSL shader source | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-signal`](spices/signal/) | Arrow-based signal processing (SF, DSP, ADSR, synth) | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-frame`](spices/frame/) | In-memory dataframe (Arrow-compatible columnar) | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-plot`](spices/plot/) | 2D data visualization (functions, points, histograms, contours) | 1 | ✅ | ✅ | ✅ | ✅ | tur-plutovg |
| [`tur-crdt`](spices/crdt/) | Conflict-free replicated data types: counters, sets, registers, maps and a replicated sequence, over a join-semilattice, with delta-state mutators | 1 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-tourist-ws`](spices/tourist-ws/) | WebSocket routes for the tur-tourist framework (ws-route!) | 1 | ✅ | ✅ | ❌ | ❌ | tur-tourist, tur-ws-server, tur-httpd, tur-json |
| [`tur-linalg`](spices/linalg/) | Dense float linear algebra: matrices, vectors, Cholesky/LU/QR solvers, mat4 graphics helpers | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-scscm`](spices/scscm/) | scscm s-expression -> sclang compiler + scsynth/hcsynth OSC client | 2 | ✅ | ✅ | ⚠️ | ⚠️ | tur-osc (optional, server module only) |
| [`tur-tidal`](spices/tidal/) | Tidal-like mini-notation -> Pbind/event text | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-stats`](spices/stats/) | Statistical analysis on dataframes (summary, distributions, hypothesis tests, OLS, resampling) | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-ansi`](spices/ansi/) | ANSI terminal control, raw-mode key input, color, style, inline images (Kitty/iTerm2/sixel) | 2 | ✅ | ✅ | ⚠️ | ⚠️ | -- |
| [`tur-tourist-session`](spices/tourist-session/) | Swappable-store session middleware for tur-tourist (memory + file stores) | 2 | ✅ | ✅ | ❌ | ❌ | tur-tourist, tur-httpd |
| [`tur-ecs`](spices/ecs/) | Entity-Component-System for Turmeric. E1 surface: dense + sparse + tag storages, variadic-looking for-each (up to 8 components) over the row-typed Query value, with/without tag filters, defquery + run-query! functional surface | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-ecs-raylib`](spices/ecs-raylib/) | Raylib companion to tur-ecs: standard 2D components, systems, and a with-game-loop macro for ECS-driven games | 2 | ✅ | ✅ | ✅ | ✅ | tur-raylib |
| [`tur-httpd`](spices/httpd/) | Minimal threaded HTTP/1.1 server (POSIX sockets) | 2 | ✅ | ✅ | ❌ | ❌ | tur-json, tur-http (optional), tur-tls (optional) |
| [`tur-msgpack`](spices/msgpack/) | MessagePack binary serialization for Turmeric -- the binary twin of tur-json | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-secret`](spices/secret/) | Linear key material and crypto hygiene primitives for Turmeric | 2 | ✅ | ✅ | ✅ | ❌ | -- |
| [`tur-template`](spices/template/) | ERB/EJS-style string templating engine | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-thread-pool`](spices/thread-pool/) | Bounded POSIX worker-thread pool for Turmeric (typed, linear Pool<T> handle; futures; with-pool) | 2 | ✅ | ✅ | ❌ | ❌ | -- |
| [`tur-tourist`](spices/tourist/) | Sinatra/scotty-style HTTP micro-framework | 2 | ✅ | ✅ | ❌ | ❌ | tur-httpd, tur-http (optional) |
| [`tur-tourist-session-valkey`](spices/tourist-session-valkey/) | Valkey-backed session store for tur-tourist-session | 2 | ✅ | ✅ | ❌ | ❌ | tur-tourist-session, tur-valkey |
| [`tur-watch`](spices/watch/) | Cross-platform filesystem watching (inotify + kqueue) with debounce and coalescing for CLI tools | 2 | ✅ | ✅ | ❌ | ❌ | -- |
| [`tur-ws-core`](spices/ws-core/) | Shared RFC 6455 WebSocket protocol types + handshake crypto | 2 | ✅ | ✅ | ✅ | ✅ | -- |
| [`tur-ws-server`](spices/ws-server/) | RFC 6455 WebSocket server upgrade for tur-httpd | 2 | ✅ | ✅ | ❌ | ❌ | tur-httpd |
| [`tur-opengl`](spices/opengl/) | OpenGL 3.3 Core + GLFW + GLAD bindings | 3 | ✅ | ✅ | ✅ | ⚠️ | glfw 3.4, glad v2.0.6 |
| [`tur-sqlite`](spices/sqlite/) | SQLite3 database bindings | 3 | ✅ | ✅ | ✅ | ✅ | sqlite 3.47.2 |
| [`tur-raylib`](spices/raylib/) | Raylib 5.5 graphics and input | 3 | ✅ | ✅ | ✅ | ✅ | raylib 5.5 |
| [`tur-plutovg`](spices/plutovg/) | 2D vector graphics rendering via plutovg | 3 | ✅ | ✅ | ✅ | ✅ | plutovg 1.3 |
| [`tur-json`](spices/json/) | JSON parsing and serialization | 3 | ✅ | ✅ | ✅ | ✅ | yyjson 0.10.0 |
| [`tur-http`](spices/http/) | HTTP/HTTPS client | 3 | ✅ | ✅ | ❌ | ❌ | mbedTLS 3.6.2 |
| [`tur-regex`](spices/regex/) | PCRE2 regex bindings | 3 | ✅ | ✅ | ✅ | ✅ | PCRE2 10.44 |
| [`tur-notebook`](spices/notebook/) | Literate `.tur.md` notebooks with TUI, HTML export, and cell execution | 3 | ✅ | ✅ | ❌ | ❌ | libturi (linked against turmeric build) |
| [`tur-osc`](spices/osc/) | Open Sound Control (OSC) messaging via liblo | 3 | ✅ | ✅ | ? | ❌ | liblo 0.32 |
| [`tur-png`](spices/png/) | PNG image read/write via libpng | 3 | ✅ | ✅ | ✅ | ✅ | libpng 1.6.43 |
| [`tur-postgres`](spices/postgres/) | PostgreSQL client via libpq | 3 | ✅ | ✅ | ✅ | ❌ | libpq (system) |
| [`tur-rtaudio`](spices/rtaudio/) | Cross-platform audio I/O via RtAudio | 3 | ✅ | ✅ | ✅ | ❌ | RtAudio 6.0.1 |
| [`tur-rtmidi`](spices/rtmidi/) | Cross-platform MIDI I/O via RtMidi | 3 | ✅ | ✅ | ✅ | ❌ | RtMidi 6.0.0 |
| [`tur-sdf-raylib`](spices/sdf-raylib/) | SDF-based solid modeling with raylib rendering and colored mesh export | 3 | ✅ | ✅ | ✅ | ⚠️ | raylib 5.5 |
| [`tur-valkey`](spices/valkey/) | Valkey/Redis client via hiredis | 3 | ✅ | ✅ | ✅ | ❌ | hiredis 1.2.0 |
| [`tur-nng`](spices/nng/) | nanomsg-next-generation scalability protocols (req/rep, pub/sub, pipeline, pair, bus, survey) | 3 | ✅ | ✅ | ✅ | ❌ | nng 1.12.4 |
| [`tur-wav`](spices/wav/) | WAV and PCM audio file read/write via libsndfile | 3 | ✅ | ✅ | ✅ | ✅ | libsndfile 1.2.2 |
| [`tur-raygui`](spices/raygui/) | Immediate-mode GUI controls for Turmeric, layered on tur-raylib | 3 | ✅ | ✅ | ✅ | ✅ | raygui 4.0 |
| [`tur-tls`](spices/tls/) | TLS termination for tur/httpd via mbedTLS | 3 | ✅ | ✅ | ❌ | ❌ | mbedTLS 3.6.2 |
| [`tur-ws-client`](spices/ws-client/) | RFC 6455 WebSocket client (ws:// and wss://) for Turmeric | 3 | ✅ | ✅ | ❌ | ❌ | mbedTLS 3.6.2 (wss:// only) |
| [`tur-zlib`](spices/zlib/) | zlib + gzip encode/decode for Turmeric | 3 | ✅ | ✅ | ✅ | ✅ | zlib 1.3.1 |

### Platform notes

- **Browser file I/O.** Emscripten gives a program a virtual filesystem, so
  anything a spice reads or writes by path (`tur-frame` CSVs, `tur-plot`'s TTF
  font, `tur-png`/`tur-wav` files, a `tur-sqlite` database) has to be embedded
  or preloaded into the page. `tur-sqlite` is in memory by default; durable
  storage needs IDBFS.
- **`tur-ansi`** is portable except for `ansi/term`, which is `termios` +
  `sys/ioctl` + `sys/select` for raw mode, terminal size and resize signals.
  Windows needs a Console-API port of that one module; a browser has no tty.
- **`tur-scscm`**'s s-expression -> sclang compiler is pure text
  transformation and portable; only its OSC client module inherits the
  `tur-osc` limits below.
- **`tur-opengl`** reaches the browser through Emscripten's GLFW port
  (`-sUSE_GLFW=3`) restricted to the GLES 3.0 / WebGL2 subset. Nothing in the
  manifest wires that up yet.
- **`tur-raylib`** is first-class on the web: its `build.tur` carries
  `:wasm-options` (`PLATFORM=Web`, `GRAPHICS_API_OPENGL_ES3`) and `raylib/web`
  supplies the frame-callback loop a browser requires instead of a blocking
  `while` loop. **`tur-sdf-raylib`** inherits that dependency, but its
  `raylib/integration` preview windows drive a blocking
  `while (!WindowShouldClose())` loop from inline C, which a browser cannot run
  without Asyncify.
- **Socket clients and servers.** `tur-http` calls BSD sockets
  (`sys/socket.h`, `netdb.h`) directly, and `tur-tourist-session` builds on
  `tur-httpd`, whose server is BSD sockets + pthreads. Both need a Winsock
  port for Windows. No browser offers raw TCP or UDP, so `tur-http` would have
  to be remapped onto `fetch()`, and `tur-osc`, `tur-postgres`, `tur-valkey`
  and `tur-nng` have no browser story at all.
- **`tur-osc`** is the one unaudited cell: liblo's own Windows support is
  partial, and the spice links a system liblo rather than building it from
  source (see the CI notes on why). Neither half has been tried on MinGW.
- **`tur-notebook`** drives a `termios`/`sys/ioctl` TUI and links `libturi` out
  of a turmeric build; neither part has a Windows or browser path today.
- **`tur-rtaudio` and `tur-rtmidi`** support Windows upstream (WASAPI, WinMM).
  In a browser they would need Web Audio (`AudioWorklet`) and Web MIDI
  backends respectively; neither exists yet.

---

## Quick Start

All spices live in a single monorepo. Reference any of them with a `:subdir`
key in your `build.tur`:

```turmeric
:spices {
  "test"   {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "test-v0.1.0"
            :subdir "spices/test"
            :optional true}
  "math"   {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "math-v0.1.0"
            :subdir "spices/math"}
  "sqlite" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "sqlite-v0.1.0"
            :subdir "spices/sqlite"}
}
```

Or use `tur add` from the command line:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref test-v0.1.0 --subdir spices/test --name test
```

---

## Spice Reference

### tur-test -- testing utilities

```turmeric
(import test :refer [deftest assert-eq assert-err run-tests])

(deftest "adds correctly"
  (assert-eq (+ 1 2) 3))

(run-tests)
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref test-v0.1.0 --subdir spices/test --name test
```

---

### tur-math -- 2D/3D vector and matrix math

Exports: `math/vec2`, `math/vec3`, `math/mat4`

```turmeric
(import math/vec3 :refer [vec3 dot cross normalize])

(let [a (vec3 1.0 0.0 0.0)
      b (vec3 0.0 1.0 0.0)]
  (println (cross a b)))  ; => (0.0 0.0 1.0)
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref math-v0.1.0 --subdir spices/math --name math
```

---

### tur-c-dsl -- C99 code generation DSL

Exports: `c-dsl/codegen`, `c-dsl/types`, `c-dsl/core`, `c-dsl/fns`,
         `c-dsl/mem`, `c-dsl/pp`, `c-dsl/typedef`, `c-dsl/builtins`

Write C99 code using Lisp syntax and compile it to a source string:

```turmeric
(import c-dsl/codegen  :refer [compile-c c-stmts])
(import c-dsl/types    :refer [c-type c-ptr-type])
(import c-dsl/core     :refer [c-let1 c-for1 c-set! c-return c-binop c-inc!])
(import c-dsl/fns      :refer [c-param c-defn c-call])
(import c-dsl/mem      :refer [c-index c-sizeof])
(import c-dsl/pp       :refer [c-include-sys])

;; int32_t array_sum(int32_t* arr, int32_t len) { ... }
(let [body (c-stmts
             (cons (c-let1 "sum" ":int" "0")
                   (cons (c-for1 "i" ":int" "0" "(i < len)" "i++"
                           (c-set! "sum" (c-binop "+" "sum" (c-index "arr" "i"))))
                         (cons (c-return "sum") 0))))
      fn   (c-defn "array_sum"
                   (cons (c-param "arr" (c-ptr-type ":int"))
                         (cons (c-param "len" ":int") 0))
                   ":int" body)
      src  (compile-c (cons (c-include-sys "stdint.h") (cons fn 0)))]
  (println src))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref c-dsl-v0.1.0 --subdir spices/c-dsl --name c-dsl
```

---

### tur-glsl -- GLSL shader DSL

Exports: `glsl/codegen`, `glsl/types`, `glsl/core`, `glsl/shaders`, `glsl/builtins`, `glsl/stdlib`

Write GLSL vertex and fragment shaders using Lisp syntax and compile them to source strings:

```turmeric
(import glsl/shaders  :refer [glsl-vertex-shader glsl-fragment-shader
                               glsl-input glsl-output glsl-uniform])
(import glsl/core     :refer [glsl-let glsl-set! glsl-stmts])
(import glsl/builtins :refer [vec4 normalize dot mix])
(import glsl/codegen  :refer [compile-glsl])

(def vert-src
  (compile-glsl
    (glsl-vertex-shader "330 core"
      (cons (glsl-input "aPos" ":vec3" 0)
        (cons (glsl-uniform "model" ":mat4")
          (cons (glsl-uniform "view" ":mat4")
            (cons (glsl-uniform "projection" ":mat4") 0))))
      (glsl-set! "gl_Position"
        "(projection * view * model * vec4(aPos, 1.0))"))))

(def frag-src
  (compile-glsl
    (glsl-fragment-shader "330 core"
      (cons (glsl-output "FragColor" ":vec4") 0)
      (glsl-set! "FragColor" "vec4(1.0, 0.5, 0.2, 1.0)"))))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref glsl-v0.1.0 --subdir spices/glsl --name glsl
```

---

### tur-opengl -- OpenGL 3.3 Core + GLFW + GLAD

Exports: `opengl/window`, `opengl/buffers`, `opengl/shaders`, `opengl/textures`,
         `opengl/draw`, `opengl/input`, `opengl/math`

A complete modern OpenGL 3.3 Core binding. Pairs with `tur-glsl` for authoring
vertex and fragment shaders in Turmeric syntax.

```turmeric
(import opengl/window  :refer [with-window window-should-close?
                                poll-events swap-buffers set-clear-color clear])
(import opengl/buffers :refer [make-vao make-vbo bind-vao bind-vbo
                                upload-vertices vertex-attrib])
(import opengl/shaders :refer [compile-shader shader-program with-program])
(import opengl/draw    :refer [draw-arrays depth-test])
(import opengl/math    :refer [mat4-perspective mat4-look-at mat4-rotate-y
                                mat4-ptr])

(defn main [] :int
  (with-window w 800 600 "Hello Triangle"
    (depth-test)
    (let [vao  (make-vao)
          vbo  (make-vbo)
          prog (shader-program
                 (compile-shader ":vertex"   vert-glsl)
                 (compile-shader ":fragment" frag-glsl))]
      (bind-vao vao)
      (bind-vbo vbo)
      (upload-vertices vertex-data (* 9 4) ":static-draw")
      (vertex-attrib 0 3 ":float" false 12 0)
      (set-clear-color 0.1 0.1 0.1 1.0)
      (while (not (window-should-close? w))
        (clear)
        (with-program prog
          (bind-vao vao)
          (draw-arrays ":triangles" 0 3))
        (swap-buffers w)
        (poll-events))))
  0)
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref opengl-v0.1.0 --subdir spices/opengl --name opengl
```

---

### tur-sqlite -- SQLite3 bindings

Exports: `sqlite/db`, `sqlite/stmt`, `sqlite/row`

```turmeric
(import sqlite/db  :refer [db-open db-close db-query])
(import sqlite/row :refer [row-get])

(let [db (ok-val (db-open "app.db"))]
  (db-exec db "CREATE TABLE IF NOT EXISTS kv (k TEXT, v TEXT)")
  (db-exec db "INSERT INTO kv VALUES ('hello', 'world')")
  (let [rows (ok-val (db-query db "SELECT * FROM kv"))]
    (for [row rows]
      (println (row-get row "k") (row-get row "v"))))
  (db-close db))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref sqlite-v0.1.0 --subdir spices/sqlite --name sqlite
```

---

### tur-raylib -- Raylib 5.5 graphics and input

Exports: `raylib/core`, `raylib/shapes`, `raylib/textures`, `raylib/text`,
         `raylib/models`, `raylib/camera`, `raylib/audio`, `raylib/color`,
         `raylib/input`

```turmeric
(import raylib/core   :refer [init-window close-window window-should-close
                               begin-drawing end-drawing clear-background
                               set-target-fps])
(import raylib/text   :refer [draw-text])
(import raylib/color  :refer [raywhite black])

(init-window 800 450 "hello")
(set-target-fps 60)
(while (not (window-should-close))
  (begin-drawing)
  (clear-background (raywhite))
  (draw-text "Hello, Turmeric!" 190 200 20 (black))
  (end-drawing))
(close-window)
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref raylib-v0.1.0 --subdir spices/raylib --name raylib
```

---

### tur-plutovg -- 2D vector graphics

Exports: `plutovg/surface`, `plutovg/canvas`, `plutovg/path`, `plutovg/paint`,
`plutovg/font`

Off-screen 2D rendering: paths, fills, strokes, gradients, textures, text,
and PNG/JPEG export -- the same engine that backs LunaSVG / PlutoSVG.
A natural companion to `tur-png` when you need pixel-level access to the
rasterised output.

```turmeric
(import plutovg/surface :refer [surface-create surface-write-png surface-destroy])
(import plutovg/canvas  :refer [canvas-create canvas-destroy
                                canvas-set-source-color
                                canvas-circle canvas-fill])

(let [s (ok-val (surface-create 256 256))
      c (ok-val (canvas-create s))]
  (canvas-set-source-color c 0.9 0.2 0.2 1.0)
  (canvas-circle c 128.0 128.0 96.0)
  (canvas-fill c)
  (surface-write-png s "circle.png")
  (canvas-destroy c)
  (surface-destroy s))
```

For the full tour (paths, gradients, fonts, image composition) see
[`docs/guides/plutovg-guide.md`](docs/guides/plutovg-guide.md).

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref plutovg-v0.1.0 --subdir spices/plutovg --name plutovg
```

---

### tur-json -- JSON parsing and serialization

Exports: `json/parse`, `json/emit`, `json/patch`

```turmeric
(import json/parse :refer [json-parse json-free])
(import json/emit  :refer [json-emit json-emit-pretty])
(import json/patch :refer [json-get-in json-set-in])

(let [r (json-parse "{\"user\":{\"name\":\"Alice\",\"age\":30}}")]
  (when (ok? r)
    (let [doc (ok-val r)]
      (println (json-get-in doc "user.name"))  ; "Alice"
      (println (json-emit-pretty doc))
      (json-free doc))))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref json-v0.1.0 --subdir spices/json --name json
```

---

### tur-http -- HTTP/HTTPS client

Exports: `http/client`, `http/request`, `http/response`, `http/error`

```turmeric
(import http/client   :refer [http-get http-post])
(import http/response :refer [response-status response-body])

(let [r (http-get "https://httpbin.org/get")]
  (when (ok? r)
    (let [resp (ok-val r)]
      (println (response-status resp))
      (println (response-body resp)))))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref http-v0.1.0 --subdir spices/http --name http
```

---

### tur-regex -- PCRE2 regex bindings

Exports: `regex/regex`, `regex/capture`, `regex/error`

```turmeric
(import regex/regex   :refer [regex-compile regex-match regex-replace])
(import regex/capture :refer [capture-at capture-named])

(let [r (regex-compile "(?P<year>\\d{4})-(?P<month>\\d{2})" 0)]
  (when (ok? r)
    (let [re (ok-val r)
          m  (regex-match re "Today is 2026-05-21")]
      (when (ok? m)
        (println (capture-named (ok-val m) "year"))   ; 2026
        (println (capture-named (ok-val m) "month"))) ; 05
      (regex-free re))))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref regex-v0.1.0 --subdir spices/regex --name regex
```

---

### tur-frame -- in-memory dataframe (Arrow-compatible columnar)

Exports: `frame/type`, `frame/buffer`, `frame/column`, `frame/schema`, `frame/frame`,
`frame/select`, `frame/filter`, `frame/sort`, `frame/group`, `frame/join`,
`frame/reshape`, `frame/csv`, `frame/print`, `frame/interop`.

```turmeric
(import frame/csv    :refer [read-csv-string])
(import frame/select :refer [select-cols])
(import frame/filter :refer [filter-mask])
(import frame/group  :refer [group-by agg agg-sum])
(import frame/print  :refer [print-frame])

(let [f       (read-csv-string "g,v\nA,10\nB,20\nA,30\n" 0 0 1 0 "")
      g       (group-by f (cons "g" 0))
      outs    (cons "total" 0)
      ins     (cons "v" 0)
      tags    (cons (agg-sum) 0)
      summary (agg g outs ins tags)]
  (print-frame summary))     ;; | g | total | ...
```

Storage layout matches Apache Arrow's in-memory columnar format (validity
bitmap + values + offsets + 64-byte alignment), so frames can be handed to
PyArrow / nanoarrow / DuckDB / Polars over the [Arrow C Data Interface](https://arrow.apache.org/docs/format/CDataInterface.html)
via `frame/interop`'s `arrow-export` / `arrow-import`.

See [`docs/guides/frame-guide.md`](docs/guides/frame-guide.md) for the full
walkthrough (building, selecting, filtering, sorting, grouping, joining,
reshaping, and Arrow interop).

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref frame-v0.1.0 --subdir spices/frame --name frame
```

---

### tur-stats -- statistical analysis on dataframes

Exports: `stats/mathx`, `stats/rng`, `stats/summary`, `stats/cov`,
`stats/dist`, `stats/test`, `stats/regress`, `stats/sample`, `stats/fmt`

```turmeric
(import frame/csv    :refer [read-csv-string])
(import stats/summary :refer [col-mean col-sd describe])
(import stats/dist   :refer [dnorm pnorm qnorm rnorm])
(import stats/test   :refer [t-test-2samp])

;; Summary statistics
(let [f (read-csv-string "a,b\n1,2\n3,4\n5,6\n" 0 0 1 0 "")]
  (describe f))

;; Normal distribution
(println (dnorm 0.0 0.0 1.0))  ; PDF
(println (pnorm 1.96 0.0 1.0)) ; CDF

;; t-test
(let [a (read-csv-string "x\n1\n2\n3\n" 0 0 1 0 "")
      b (read-csv-string "x\n4\n5\n6\n" 0 0 1 0 "")]
  (t-test-2samp a b))
```

Add to your project:

```sh
tur add https://github.com/turmeric-lang/turmeric-spices \
  --ref stats-v0.1.0 --subdir spices/stats --name stats
```

---

## Tag convention

Each spice is tagged independently:

```
c-dsl-v0.1.0
frame-v0.1.0
glsl-v0.1.0
http-v0.1.0
json-v0.1.0
math-v0.1.0
opengl-v0.1.0
plutovg-v0.1.0
raylib-v0.1.0
regex-v0.1.0
sqlite-v0.1.0
stats-v0.1.0
test-v0.1.0
```

Always pin to a tag in production. Omitting `:ref` resolves to HEAD and
prints a warning.

---

## Development

This repo is a Turmeric workspace. To build all spices:

```sh
just build
```

To run tests for a spice:

```sh
cd spices/test && tur test
cd spices/math && tur test
```

C/CMake deps (Tier 3) are fetched via `tur fetch` and built with CMake
at compile time. No manual CMake invocation is needed.
