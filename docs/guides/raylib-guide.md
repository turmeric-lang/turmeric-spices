---
title: Raylib Graphics and Input
category: Graphics and Games
description: Windows, shapes, text, textures, audio, camera, and input -- the full small-game stack with raylib 5.5
audience: developers building games, prototypes, demos, or learning-oriented graphics code
since: raylib v0.1.0
---

# tur-raylib Guide

`tur-raylib` binds raylib 5.5 to Turmeric: window management, 2D shape and
text primitives, textures, 3D camera helpers, audio playback, and
keyboard / gamepad input. Use it for prototypes, jam games, demos, and
learning-oriented graphics code where raylib's "batteries-included" model
is a better fit than raw GL.

This guide walks the six things you'll do most often:

1. [Window and game loop](#1-window-and-game-loop)
2. [Shapes and text](#2-shapes-and-text)
3. [Textures and images](#3-textures-and-images)
4. [Input: keyboard and mouse](#4-input-keyboard-and-mouse)
5. [Audio](#5-audio)
6. [3D camera and models](#6-3d-camera-and-models)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "raylib" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "raylib-v0.1.0"
             :subdir "spices/raylib"}
}
```

Then `tur fetch`. The spice pulls in `raylib 5.5` via CMake -- no manual
build step. On first `tur build` the dependency is fetched and compiled
automatically.

---

## 1. Window and game loop

Every raylib program follows the same shape: open a window, loop until the
user closes it, draw a frame each iteration, close the window.

```turmeric
(import raylib/core  :refer [init-window close-window window-should-close
                             begin-drawing end-drawing clear-background
                             set-target-fps])
(import raylib/text  :refer [draw-text])
(import raylib/color :refer [raywhite black])

(init-window 800 450 "hello")
(set-target-fps 60)
(while (not (window-should-close))
  (begin-drawing)
  (clear-background (raywhite))
  (draw-text "Hello, Turmeric!" 190 200 20 (black))
  (end-drawing))
(close-window)
```

```sweet-exp
#lang sweet-exp
import raylib/core  :refer [init-window close-window window-should-close
                             begin-drawing end-drawing clear-background
                             set-target-fps]
import raylib/text  :refer [draw-text]
import raylib/color :refer [raywhite black]

init-window(800 450 "hello")
set-target-fps(60)
while not(window-should-close())
  begin-drawing()
  clear-background(raywhite())
  draw-text("Hello, Turmeric!" 190 200 20 black())
  end-drawing()
close-window()
```

### Frame timing

`set-target-fps` caps the frame rate. `get-frame-time` returns the seconds
elapsed since the last frame, for frame-rate-independent movement:

```turmeric
(import raylib/core :refer [get-frame-time])

(let [dt (get-frame-time)]
  (set! x (+ x (* speed dt))))   ;; move `speed` units per second
```

---

## 2. Shapes and text

### 2D shapes

```turmeric
(import raylib/shapes :refer [draw-circle draw-rectangle draw-line
                              draw-triangle])
(import raylib/color  :refer [red blue green]))

(draw-circle 400 300 50.0 (red))           ;; filled circle
(draw-rectangle 10 10 100 50 (blue))       ;; filled rectangle
(draw-line 0 0 800 450 (green))            ;; line from (0,0) to (800,450)
```

All shape functions take a `Color` handle from `raylib/color`. Colors are
constructed with `(color r g b a)` where each channel is 0-255, or use the
named presets: `red`, `green`, `blue`, `white`, `black`, `gray`, `yellow`,
`orange`, `purple`, `pink`, `skyblue`, `raywhite`.

### Text

```turmeric
(import raylib/text :refer [draw-text draw-text-ex measure-text
                            load-font unload-font])

;; Default font, integer position, integer size
(draw-text "Hello" 190 200 20 (black))

;; Custom font with float position and spacing
(let [font (load-font "roboto.ttf")]
  (draw-text-ex font "Hello" 190.0 200.0 24.0 2.0 (black))
  (unload-font font))
```

`Font` is a `:linear` opaque: a handle from `load-font` must be released
exactly once with `unload-font`. Under `-Xsubstructural` this becomes a
compile-time error; in ordinary builds it is inert.

---

## 3. Textures and images

```turmeric
(import raylib/textures :refer [load-texture unload-texture
                                draw-texture draw-texture-v])
(import raylib/color    :refer [white])

(let [tex (load-texture "player.png")]
  (draw-texture tex 100 100 (white))       ;; draw at (100, 100), no tint
  (unload-texture tex))
```

`Texture2D` is a `:linear` opaque: a handle from `load-texture` must be
released exactly once with `unload-texture`. The draw operations take it by
`^borrow` -- they do not consume the handle.

### Linear resource handles

The handles with an `unload-*` peer -- `Texture2D`, `Font`, `Model`,
`Sound`, `Music` -- are `:linear` opaques. Under `-Xsubstructural`:

- Use-after-unload is a compile-time error (`TUR-E0101`)
- Leaked GPU/audio resources are a compile-time error (`TUR-E0100`)

The discipline is inert in ordinary builds, so existing call sites compile
unchanged. The value-like opaques (`Color`, `Vector2`, `Rectangle`,
`Camera2D`, `Camera3D`, `Mesh`, `Material`, `Matrix`) are nominally
distinct -- mixing them up is a type error -- but have no deleter, so they
stay plain.

---

## 4. Input: keyboard and mouse

```turmeric
(import raylib/input :refer [is-key-down is-key-pressed is-key-released
                             is-mouse-button-down is-mouse-button-pressed
                             get-mouse-position get-mouse-wheel-move])

;; Keyboard: key codes are raylib KeyboardKey integers (e.g. 65 = A)
(if (is-key-down 65)
  (set! x (+ x 1)))                       ;; move right while A is held

(if (is-key-pressed 32)
  (println "space pressed"))             ;; fire on space press

;; Mouse
(let [pos (get-mouse-position)]
  (let [mx (cons-first pos)
        my (cons-second pos)]
    (draw-circle mx my 5.0 (red))))

(if (is-mouse-button-pressed 0)           ;; 0 = left button
  (println "click!"))

(let [wheel (get-mouse-wheel-move)]
  (set! zoom (+ zoom (* wheel 0.1))))
```

Key codes are raylib's `KeyboardKey` enum values as integers. See the
[raylib keyboard key reference](https://github.com/raysan5/raylib/blob/master/src/raylib.h)
for the full list. Common ones: `32` (Space), `257` (Enter), `258` (Tab),
`262`/`263`/`264`/`265` (Left/Up/Right/Down arrows), `256` (Escape).

---

## 5. Audio

```turmeric
(import raylib/audio :refer [init-audio-device close-audio-device
                             load-sound unload-sound play-sound
                             load-music-stream unload-music-stream
                             play-music-stream update-music-stream])

(init-audio-device)
(let [sfx (load-sound "jump.wav")]
  (play-sound sfx)
  ;; ... game loop ...
  (unload-sound sfx))
(close-audio-device)
```

`Sound` is for short effects loaded entirely into memory. `Music` is for
streamed audio (longer tracks):

```turmeric
(let [music (load-music-stream "background.ogg")]
  (play-music-stream music)
  (while (not (window-should-close))
    (update-music-stream music)
    ;; ... draw frame ...
    )
  (unload-music-stream music))
```

Both `Sound` and `Music` are `:linear` opaques -- release with
`unload-sound` / `unload-music-stream` exactly once.

---

## 6. 3D camera and models

```turmeric
(import raylib/camera :refer [camera-3d update-camera])
(import raylib/core   :refer [begin-mode-3d end-mode-3d])
(import raylib/models :refer [draw-model load-model unload-model])
(import raylib/color  :refer [white])

(let [cam (camera-3d 0.0 10.0 10.0      ;; position
                       0.0  0.0  0.0    ;; target
                       0.0  1.0  0.0    ;; up
                       45.0 0)]         ;; fovy, projection (0=perspective)
  (begin-mode-3d cam)
  (let [model (load-model "cube.obj")]
    (draw-model model 0.0 0.0 0.0 1.0 (white))
    (unload-model model))
  (end-mode-3d))
```

`camera-3d` takes position, target, up vector, field-of-view in degrees,
and projection mode (`0`=perspective, `1`=orthographic). `update-camera`
applies built-in camera controls (arrow keys to orbit, mouse wheel to
zoom) when called each frame.

`Model` is a `:linear` opaque -- release with `unload-model`.

---

## Web target

`raylib/web` provides `run-main-loop` and `with-web-game-loop` for
Emscripten / WASM builds. The game loop is adapted to the browser's
`requestAnimationFrame` instead of a blocking `while` loop:

```turmeric
(import raylib/web :refer [with-web-game-loop])

(with-web-game-loop
  (fn []
    ;; update
    (if (is-key-down 65) (set! x (+ x 1))))
  (fn []
    ;; draw
    (begin-drawing)
    (clear-background (raywhite))
    (draw-text "WASM" 190 200 20 (black))
    (end-drawing)))
```

See the [web emscripten tutorial](https://turmeric-lang.com/docs/html/guides/web-emscripten-tutorial.html)
for the full WASM build pipeline.

---

## Integration with other spices

- **`tur-math`**: `vec2`, `vec3`, `mat4` for transform math. raylib's
  `Vector2` is a separate opaque, but the math spice provides the
  arithmetic you need for movement and physics.
- **`tur-raygui`**: immediate-mode GUI controls layered on top of
  raylib. See the [raygui README](../../spices/raygui/README.md).
- **`tur-ecs-raylib`**: ECS integration for raylib -- systems that read
  components and draw via raylib. See the
  [ECS guide](https://turmeric-lang.com/docs/html/guides/ecs-guide.html).
- **`tur-sdf-raylib`**: SDF solid modeling with raylib rendering. See the
  [sdf-raylib README](../../spices/sdf-raylib/README.md).
- **`tur-opengl`**: raw OpenGL bindings when raylib's abstraction is too
  high-level.

---

## See also

- [API reference](api/)
- [README](../../spices/raylib/README.md) -- linear resource handle details
- [raylib wiki](https://github.com/raysan5/raylib/wiki) -- upstream docs
- [ECS guide](https://turmeric-lang.com/docs/html/guides/ecs-guide.html) -- ECS + raylib integration
