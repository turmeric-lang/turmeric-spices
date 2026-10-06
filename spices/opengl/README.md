# tur-opengl

OpenGL 3.3 Core + GLFW + GLAD bindings for Turmeric. A complete modern-GL
binding with window/context creation, buffer and shader helpers, draw calls,
and small matrix utilities.

## Overview

`tur-opengl` is a Tier 2 spice (`cmake-dep` -- pulls in `glfw 3.4` and
`glad v2.0.6` via `tur fetch`). It exposes the common GL surface: windows,
vertex arrays, buffers, shader programs, textures, draw calls, and input.

Pairs naturally with `tur-glsl` for shader authoring and `tur-math` for the
linear-algebra side. Use it for desktop graphics demos, tools, and
games-of-modest-size that want direct GL access.

## Install

```turmeric no-check
:spices {
  "opengl" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "opengl-v0.1.0"
            :subdir "spices/opengl"}
}
```

## Quick start

```turmeric
(import opengl/window :refer [make-window destroy-window window-should-close?
                              poll-events swap-buffers set-clear-color clear])

(let [w (make-window 800 600 "Hello")]
  (set-clear-color 0.1 0.1 0.1 1.0)
  (while (not (window-should-close? w))
    (clear)
    (swap-buffers w)
    (poll-events))
  (destroy-window w))
```

```sweet-exp
#lang sweet-exp
import opengl/window :refer [make-window destroy-window window-should-close?
                              poll-events swap-buffers set-clear-color clear]

let [w make-window(800 600 "Hello")]
  set-clear-color(0.1 0.1 0.1 1.0)
  while not(window-should-close?(w))
    clear()
    swap-buffers(w)
    poll-events()
  destroy-window(w)
```

### Rainbow triangle

The classic first triangle, with the three corners colored red, green and blue
and the interior interpolated between them by the rasterizer.

Positions and colors are constants indexed by `gl_VertexID`, so there is no
vertex buffer and no attribute wiring at all -- but the core profile still
refuses to draw without a bound VAO, which is what `with-vao` supplies.
`with-program` installs the program for the draw and uninstalls it after;
it borrows, so the program is deleted separately once the loop ends.

```turmeric
(import opengl/window  :refer [with-window window-should-close? poll-events
                               swap-buffers set-clear-color clear])
(import opengl/buffers :refer [with-vao])
(import opengl/shaders :refer [compile-shader shader-program with-program
                               delete-program])
(import opengl/draw    :refer [draw-arrays])

(def vert-src
  "#version 330 core
const vec2 POS[3] = vec2[3](vec2(0.0, 0.6), vec2(-0.6, -0.4), vec2(0.6, -0.4));
const vec3 COL[3] = vec3[3](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
out vec3 vcolor;
void main() {
  gl_Position = vec4(POS[gl_VertexID], 0.0, 1.0);
  vcolor = COL[gl_VertexID];
}")

(def frag-src
  "#version 330 core
in vec3 vcolor;
out vec4 frag;
void main() { frag = vec4(vcolor, 1.0); }")

(with-window w 800 600 "Rainbow triangle"
  (let [prog (shader-program (compile-shader ":vertex" vert-src)
                             (compile-shader ":fragment" frag-src))]
    (set-clear-color 0.08 0.08 0.10 1.0)
    (with-vao vao
      (while (not (window-should-close? w))
        (clear)
        (with-program prog
          (draw-arrays ":triangles" 0 3))
        (swap-buffers w)
        (poll-events)))
    (delete-program prog)))
```

```sweet-exp
#lang sweet-exp
import opengl/window  :refer [with-window window-should-close? poll-events
                              swap-buffers set-clear-color clear]
import opengl/buffers :refer [with-vao]
import opengl/shaders :refer [compile-shader shader-program with-program
                              delete-program]
import opengl/draw    :refer [draw-arrays]

def vert-src
  "#version 330 core
const vec2 POS[3] = vec2[3](vec2(0.0, 0.6), vec2(-0.6, -0.4), vec2(0.6, -0.4));
const vec3 COL[3] = vec3[3](vec3(1,0,0), vec3(0,1,0), vec3(0,0,1));
out vec3 vcolor;
void main() {
  gl_Position = vec4(POS[gl_VertexID], 0.0, 1.0);
  vcolor = COL[gl_VertexID];
}"

def frag-src
  "#version 330 core
in vec3 vcolor;
out vec4 frag;
void main() { frag = vec4(vcolor, 1.0); }"

with-window w 800 600 "Rainbow triangle"
  let [prog shader-program(compile-shader(":vertex" vert-src)
                           compile-shader(":fragment" frag-src))]
    set-clear-color(0.08 0.08 0.10 1.0)
    with-vao vao
      while not(window-should-close?(w))
        clear()
        with-program prog
          draw-arrays(":triangles" 0 3)
        swap-buffers(w)
        poll-events()
    delete-program(prog)
```

#### The same triangle, from a vertex buffer

Real geometry comes from a buffer. `opengl/arrays` supplies the packed
`GLfloat` block: Turmeric's `:float` is a float64 while a `GL_FLOAT` attribute
is 32-bit, so vertex data has to be narrowed and packed rather than handed
over as a `(Vec float)`. `float-array-ptr` and `float-array-bytes` are exactly
the two arguments `upload-vertices` wants.

Six floats per vertex -- xyz then rgb -- so the stride is 24 bytes, position
sits at offset 0 and color at offset 12.

```turmeric
(import opengl/arrays  :refer [FloatArray with-float-array float-array-set!
                               float-array-ptr float-array-bytes])
(import opengl/buffers :refer [with-vao make-vbo bind-vbo delete-vbo
                               upload-vertices vertex-attrib])

(defn put-vertex [^borrow a : FloatArray i : int
                  x : float y : float z : float
                  r : float g : float b : float] : void
  (let [o (* i 6)]
    (float-array-set! a (+ o 0) x)
    (float-array-set! a (+ o 1) y)
    (float-array-set! a (+ o 2) z)
    (float-array-set! a (+ o 3) r)
    (float-array-set! a (+ o 4) g)
    (float-array-set! a (+ o 5) b)))

(with-vao vao
  (let [vbo (make-vbo)]
    (bind-vbo vbo)
    (with-float-array verts 18
      (put-vertex verts 0  0.0  0.6 0.0  1.0 0.0 0.0)
      (put-vertex verts 1 -0.6 -0.4 0.0  0.0 1.0 0.0)
      (put-vertex verts 2  0.6 -0.4 0.0  0.0 0.0 1.0)
      (upload-vertices (float-array-ptr verts)
                       (float-array-bytes verts)
                       ":static-draw"))
    (vertex-attrib 0 3 ":float" false 24 0)
    (vertex-attrib 1 3 ":float" false 24 12)
    (delete-vbo vbo)))
```

```sweet-exp
#lang sweet-exp
import opengl/arrays  :refer [FloatArray with-float-array float-array-set!
                              float-array-ptr float-array-bytes]
import opengl/buffers :refer [with-vao make-vbo bind-vbo delete-vbo
                              upload-vertices vertex-attrib]

defn put-vertex [^borrow a : FloatArray i : int
                 x : float y : float z : float
                 r : float g : float b : float] : void
  let [o *(i 6)]
    float-array-set!(a +(o 0) x)
    float-array-set!(a +(o 1) y)
    float-array-set!(a +(o 2) z)
    float-array-set!(a +(o 3) r)
    float-array-set!(a +(o 4) g)
    float-array-set!(a +(o 5) b)

with-vao vao
  let [vbo make-vbo()]
    bind-vbo(vbo)
    with-float-array verts 18
      put-vertex(verts 0  0.0  0.6 0.0  1.0 0.0 0.0)
      put-vertex(verts 1 -0.6 -0.4 0.0  0.0 1.0 0.0)
      put-vertex(verts 2  0.6 -0.4 0.0  0.0 0.0 1.0)
      upload-vertices(float-array-ptr(verts)
                      float-array-bytes(verts)
                      ":static-draw")
    vertex-attrib(0 3 ":float" false 24 0)
    vertex-attrib(1 3 ":float" false 24 12)
    delete-vbo(vbo)
```

The vertex shader then declares the two attributes instead of indexing
constants:

```turmeric no-check
layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 color;
out vec3 vcolor;
void main() { gl_Position = vec4(pos, 1.0); vcolor = color; }
```

### Linear `Window`; opaque GPU handles (U1)

`Window` is a `:linear` opaque: a window from `make-window` must be
destroyed exactly once with `destroy-window`, and the per-frame observers
(`window-should-close?`, `swap-buffers`, `key-pressed?`, `mouse-pos`,
`mouse-button-pressed?`) take it by `^borrow`. Use-after-destroy and leaked
windows are compile-time errors (`TUR-E0101` / `TUR-E0100`). Substructural
checking is on by default; the old `-Xsubstructural` opt-in is gone, and
passing it now warns (`TUR-W0050`).

The GPU-object handles (`Vao`, `Vbo`, `Ebo`, `Shader`, `Program`,
`Texture`) are nominally distinct opaques, so mixing them up -- e.g.
passing a `Vbo` where a `Vao` is expected -- is a type error. They are
`:linear` too, each with a consuming peer:

| Handle | Consumed by | Handle | Consumed by |
|---|---|---|---|
| `Vao` | `delete-vao` | `Program` | `delete-program` |
| `Vbo` | `delete-vbo` | `Texture` | `delete-texture` |
| `Ebo` | `delete-ebo` | `Window` | `destroy-window` |
| `Shader` | `shader-program` (links, then deletes each input) | | |

`FloatArray` from `opengl/arrays` follows the same rule, consumed by
`delete-float-array`.

Because a `Shader` is consumed by `shader-program`, reusing a shader handle
after linking is a use-after-delete rather than a second link.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/opengl-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/opengl>
