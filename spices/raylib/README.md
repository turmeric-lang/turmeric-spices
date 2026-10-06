# tur-raylib

Raylib 5.5 graphics and input bindings for Turmeric. Windows, shapes, text,
textures, audio, camera, and input -- the full small-game stack.

## Overview

`tur-raylib` is a Tier 2 spice (`cmake-dep` -- pulls in `raylib 5.5` via
`tur fetch`). It exposes the common raylib surface: window management,
`begin-drawing` / `end-drawing` frames, 2D shape and text primitives,
textures, 3D camera helpers, audio playback, and keyboard / gamepad input.

Use it for prototypes, jam games, demos, and learning-oriented graphics
code where raylib's "batteries-included" model is a better fit than raw GL.

## Install

```turmeric no-check
:spices {
  "raylib" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "raylib-v0.1.0"
            :subdir "spices/raylib"}
}
```

## Quick start

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

### Linear resource handles (U1)

The handles with an `unload-*` peer -- `Texture2D`, `Font`, `Model`,
`Sound`, `Music` -- are `:linear` opaques: a handle from its loader must be
released exactly once with the matching `unload-*`, and the draw / play /
update operations take it by `^borrow`. Under `-Xsubstructural` this makes
use-after-unload and leaked GPU/audio resources compile-time errors
(`TUR-E0101` / `TUR-E0100`); the discipline is inert in ordinary builds, so
existing call sites compile unchanged.

The value-like opaques (`Color`, `Vector2`, `Rectangle`, `Camera2D`,
`Camera3D`, `Mesh`, `Material`, `Matrix`) are nominally distinct -- mixing
them up is a type error (see `errors/swap-reject.tur`) -- but
have no deleter, so they stay plain. See `errors/` for the rejected
linear cases.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/raylib-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/raylib>
