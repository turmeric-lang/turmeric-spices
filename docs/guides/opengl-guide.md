---
title: Modern OpenGL with GLFW and GLAD
category: Graphics
description: OpenGL 3.3 Core bindings -- window creation, vertex arrays, buffers, shaders, textures, draw calls, and input, with linear GPU handles
audience: developers building desktop graphics, tools, or games-of-modest-size in Turmeric
since: opengl v0.1.0
---

# tur-opengl Guide

OpenGL 3.3 Core is a state machine: bind a vertex array, bind a buffer,
describe the layout, install a program, draw. The spice wraps that machine
in a set of nominally distinct handle types so the compiler catches the
mistakes the C API would let through -- passing a VBO where a VAO is
expected, using a shader after it has been linked, leaking a window.

This guide walks the five things you will do most often:

1. [Opening a window and clearing it](#1-window)
2. [The rainbow triangle: shaders and draw calls](#2-triangle)
3. [Vertex buffers: real geometry from data](#3-vertex-buffers)
4. [Textures and uniforms](#4-textures)
5. [Input: keyboard and mouse](#5-input)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "opengl" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "opengl-v0.1.0"
             :subdir "spices/opengl"}
}
```

Then `tur fetch`. The spice is a `cmake-dep`: it pulls in `glfw 3.4` and
`glad v2.0.6` and builds them statically. Nothing to install on the host.

Pairs naturally with `tur-glsl` for shader authoring and `tur-math` for
the linear-algebra side.

---

## The one idea

Every GPU object -- a vertex array, a buffer, a shader, a program, a
texture, a window -- is a `:linear` opaque. It is produced once and must be
consumed exactly once by its deleting peer:

| Handle | Consumed by |
|--------|-------------|
| `Vao` | `delete-vao` |
| `Vbo` | `delete-vbo` |
| `Ebo` | `delete-ebo` |
| `Shader` | `shader-program` (links, then deletes each input) |
| `Program` | `delete-program` |
| `Texture` | `delete-texture` |
| `Window` | `destroy-window` |
| `FloatArray` | `delete-float-array` |

The bind/use/upload paths take the handle by `^borrow`, observing it
without discharging the obligation. A leaked GPU object (`TUR-E0100`) and a
use-after-delete (`TUR-E0101`) are compile-time errors. Substructural
checking is on by default.

Because a `Shader` is consumed by `shader-program`, reusing a shader handle
after linking is a use-after-delete rather than a second link.

---

## 1. Window

`make-window` opens a GLFW window, creates an OpenGL 3.3 Core context, and
loads GLAD automatically. `with-window` is the macro that opens, runs a
body, and destroys:

```turmeric
(defmodule demo
  (import opengl/window :refer [with-window window-should-close? poll-events
                               swap-buffers set-clear-color clear])

  (defn main [] : int
    (with-window w 800 600 "Hello"
      (set-clear-color 0.1 0.1 0.1 1.0)
      (while (not (window-should-close? w))
        (clear)
        (swap-buffers w)
        (poll-events)))
    0))
```

```sweet-exp
defmodule demo
  import opengl/window :refer [with-window window-should-close? poll-events
                               swap-buffers set-clear-color clear]

  defn main [] : int
    with-window w 800 600 "Hello"
      set-clear-color(0.1 0.1 0.1 1.0)
      while not(window-should-close?(w))
        clear()
        swap-buffers(w)
        poll-events()
    0
```

The per-frame observers (`window-should-close?`, `swap-buffers`,
`key-pressed?`, `mouse-pos`, `mouse-button-pressed?`) take the window by
`^borrow`. Only `destroy-window` consumes it, and `with-window` calls it
for you.

---

## 2. Triangle

The classic first triangle, with the three corners colored red, green and
blue and the interior interpolated between them by the rasterizer.

Positions and colors are constants indexed by `gl_VertexID`, so there is no
vertex buffer and no attribute wiring at all -- but the core profile still
refuses to draw without a bound VAO, which is what `with-vao` supplies.
`with-program` installs the program for the draw and uninstalls it after;
it borrows, so the program is deleted separately once the loop ends.

```turmeric
(defmodule demo
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

  (defn main [] : int
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
    0))
```

```sweet-exp
defmodule demo
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

  defn main [] : int
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
    0
```

`compile-shader` prints the driver error log and calls `exit(1)` if
compilation fails; `shader-program` does the same for linking. The two
shader handles are consumed by `shader-program` -- it links them, then
deletes each one. You never call `delete-shader` on a shader you pass to
`shader-program`.

---

## 3. Vertex buffers

Real geometry comes from a buffer. `opengl/arrays` supplies the packed
`GLfloat` block: Turmeric's `:float` is a float64 while a `GL_FLOAT`
attribute is 32-bit, so vertex data has to be narrowed and packed rather
than handed over as a `(Vec float)`. `float-array-ptr` and
`float-array-bytes` are exactly the two arguments `upload-vertices` wants.

Six floats per vertex -- xyz then rgb -- so the stride is 24 bytes, position
sits at offset 0 and color at offset 12:

```turmeric
(defmodule demo
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

  (defn main [] : int
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
        (delete-vbo vbo))
    0))
```

```sweet-exp
defmodule demo
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

  defn main [] : int
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
    0
```

The vertex shader then declares the two attributes instead of indexing
constants:

```turmeric no-check
layout (location = 0) in vec3 pos;
layout (location = 1) in vec3 color;
out vec3 vcolor;
void main() { gl_Position = vec4(pos, 1.0); vcolor = color; }
```

For indexed geometry, bind an EBO inside the VAO and use `draw-elements`
instead of `draw-arrays`:

```turmeric no-check
(let [ebo (make-ebo)]
  (bind-ebo ebo)
  (upload-indices idx-data (* 36 4) ":static-draw")
  (delete-ebo ebo))
;; then in the render loop:
(draw-elements ":triangles" 36 ":unsigned-int")
```

---

## 4. Textures

A texture is created, bound, uploaded, and parameterised before it is
sampled in a shader:

```turmeric
(defmodule demo
  (import opengl/textures :refer [make-texture bind-texture delete-texture
                                upload-texture-rgba set-texture-wrap
                                set-texture-filter generate-mipmaps
                                active-texture])
  (import opengl/shaders  :refer [set-uniform-int])

  (defn setup-texture [] : Texture
    (let [tex (make-texture)]
      (bind-texture tex)
      ;; upload-texture-rgba pixels 512 512   ;; your RGBA8 pixel data
      (set-texture-wrap ":repeat" ":repeat")
      (set-texture-filter ":linear-mipmap-linear" ":linear")
      (generate-mipmaps)
      tex))
  ;; In the render loop:
  ;; (active-texture 0)
  ;; (bind-texture tex)
  ;; (set-uniform-int prog "diffuse" 0)
  )
```

```sweet-exp
defmodule demo
  import opengl/textures :refer [make-texture bind-texture delete-texture
                                upload-texture-rgba set-texture-wrap
                                set-texture-filter generate-mipmaps
                                active-texture]
  import opengl/shaders  :refer [set-uniform-int]

  defn setup-texture [] : Texture
    let [tex make-texture()]
      bind-texture(tex)
      ;; upload-texture-rgba(pixels 512 512)   ;; your RGBA8 pixel data
      set-texture-wrap(":repeat" ":repeat")
      set-texture-filter(":linear-mipmap-linear" ":linear")
      generate-mipmaps()
      tex
  ;; In the render loop:
  ;; active-texture(0)
  ;; bind-texture(tex)
  ;; set-uniform-int(prog "diffuse" 0)
```

Uniforms are set by name: `set-uniform-int`, `set-uniform-float`,
`set-uniform-vec2/3/4`, and `set-uniform-mat4`. The mat4 path takes a raw
pointer from `mat4-ptr` (in `opengl/math`), so you can build a
perspective + view + model chain and upload it:

```turmeric no-check
(import opengl/math :refer [mat4-perspective mat4-look-at mat4-translate
                           mat4-rotate-y mat4-mul mat4-ptr])

(let [proj (mat4-perspective 0.785 (/ 800.0 600.0) 0.1 100.0)
      view (mat4-look-at 0.0 0.0 3.0  0.0 0.0 0.0  0.0 1.0 0.0)
      model (mat4-translate 0.0 0.0 0.0)
      mvp   (mat4-mul proj (mat4-mul view model))]
  (set-uniform-mat4 prog "mvp" (mat4-ptr mvp)))
```

---

## 5. Input

Key and mouse state are polled per frame via GLFW:

```turmeric
(defmodule demo
  (import opengl/window :refer [with-window window-should-close? poll-events
                               swap-buffers set-clear-color clear])
  (import opengl/input  :refer [key-pressed? mouse-pos mouse-pos-x
                               mouse-pos-y mouse-button-pressed?])

  (defn main [] : int
    (with-window w 800 600 "Input"
      (set-clear-color 0.1 0.1 0.1 1.0)
      (while (not (window-should-close? w))
        (when (key-pressed? w 256)        ;; 256 = GLFW_KEY_ESCAPE
          (break))
        (let [pos (mouse-pos w)]
          (when (mouse-button-pressed? w 0)  ;; 0 = left button
            (println (mouse-pos-x pos))
            (println (mouse-pos-y pos))))
        (clear)
        (swap-buffers w)
        (poll-events)))
    0))
```

```sweet-exp
defmodule demo
  import opengl/window :refer [with-window window-should-close? poll-events
                               swap-buffers set-clear-color clear]
  import opengl/input  :refer [key-pressed? mouse-pos mouse-pos-x
                               mouse-pos-y mouse-button-pressed?]

  defn main [] : int
    with-window w 800 600 "Input"
      set-clear-color(0.1 0.1 0.1 1.0)
      while not(window-should-close?(w))
        when key-pressed?(w 256)        ;; 256 = GLFW_KEY_ESCAPE
          break
        let [pos mouse-pos(w)]
          when mouse-button-pressed?(w 0)  ;; 0 = left button
            println $ mouse-pos-x(pos)
            println $ mouse-pos-y(pos)
        clear()
        swap-buffers(w)
        poll-events()
    0
```

Key codes are GLFW integer constants (e.g. 256 for Escape, 32 for Space,
87 for W). Mouse button codes: 0 = left, 1 = right, 2 = middle.

---

## GL state toggles

`enable` and `disable` toggle server-side capabilities by keyword:
`:depth-test`, `:blend`, `:cull-face`, `:stencil-test`, `:scissor-test`,
`:multisample`. Two convenience wrappers set common defaults:

```turmeric no-check
(depth-test)    ;; glEnable(GL_DEPTH_TEST) + glDepthFunc(GL_LESS)
(blending)      ;; glEnable(GL_BLEND) + glBlendFunc(src-alpha, 1-src-alpha)
```

---

## When not to use this

- **You need a high-level scene graph or engine.** This is a direct GL
  binding. `tur-raylib` is the higher-level option; `tur-ecs-raylib` adds
  an ECS layer on top of that.
- **You need compute shaders or transform feedback.** The compute shader
  stage compiles (`compile-shader ":compute"`), but there is no dispatch
  wrapper yet.
- **You need OpenGL ES or WebGL.** The context is desktop OpenGL 3.3 Core.

---

## See also

- [API reference](api/)
- [README](../../spices/opengl/README.md) -- the full handle table and the vertex-buffer example
- [tur-math guide](math-guide.html) -- vectors, matrices, and quaternions for the linear-algebra side
- [tur-raylib guide](raylib-guide.html) -- the higher-level graphics alternative
