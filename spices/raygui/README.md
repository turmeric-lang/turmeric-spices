# tur-raygui

Immediate-mode GUI controls for Turmeric, layered on tur-raylib. Buttons,
sliders, text boxes, dropdowns, color pickers, panels, and a styleable
theme system -- the full raygui control set.

## Overview

`tur-raygui` is a Tier 3 spice (`cmake-dep` -- pulls in `raygui 4.0` via
`tur fetch`). It depends on `tur-raylib` for window, drawing, and input.
The control surface mirrors raylib's `raygui.h` one-to-one: every
`Gui*` function is a `gui-*` export, grouped by module (`raygui/controls`,
`raygui/layout`, `raygui/core`, `raygui/style`, `raygui/themes`,
`raygui/icons`).

Use it for in-app tool windows, debug inspectors, level editors, and
any immediate-mode UI that sits on top of a raylib render loop.

## Install

```turmeric no-check
:spices {
  "raygui" {:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "raygui-v0.1.0"
            :subdir "spices/raygui"}
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
(import raylib/color :refer [raywhite])
(import raygui/core     :refer [gui-load-style-default])
(import raygui/controls :refer [gui-label gui-button gui-slider gui-check-box
                                make-text-buf free-text-buf text-buf->cstr])
(import raygui/rect     :refer [rect])

(init-window 800 600 "hello-gui")
(set-target-fps 60)
(gui-load-style-default)
(let [name-buf (make-text-buf 64)]
  (while (not (window-should-close))
    (begin-drawing)
    (clear-background (raywhite))
    (gui-label (rect 10.0 10.0 200.0 20.0) "Name:")
    (gui-button (rect 10.0 180.0 120.0 30.0) "Go!")
    (end-drawing))
  (free-text-buf name-buf))
(close-window)
```

```sweet-exp
#lang sweet-exp
import raylib/core  :refer [init-window close-window window-should-close
                             begin-drawing end-drawing clear-background
                             set-target-fps]
import raylib/color :refer [raywhite]
import raygui/core     :refer [gui-load-style-default]
import raygui/controls :refer [gui-label gui-button gui-slider gui-check-box
                                make-text-buf free-text-buf text-buf->cstr]
import raygui/rect     :refer [rect]

init-window(800 600 "hello-gui")
set-target-fps(60)
gui-load-style-default()
let [name-buf make-text-buf(64)]
  while not(window-should-close())
    begin-drawing()
    clear-background(raywhite())
    gui-label(rect(10.0 10.0 200.0 20.0) "Name:")
    gui-button(rect(10.0 180.0 120.0 30.0) "Go!")
    end-drawing()
  free-text-buf(name-buf)
close-window()
```

### Modules

| Module | Exports |
|--------|---------|
| `raygui/rect` | `Rect`, `rect` |
| `raygui/core` | enable/disable, lock, state, font, style |
| `raygui/controls` | label, button, toggle, slider, spinner, text box, combo box, dropdown, list view, color picker, grid |
| `raygui/layout` | window box, group box, panel, scroll panel, tab bar, status bar |
| `raygui/style` | style property constants and setters |
| `raygui/themes` | `gui-apply-light-theme`, `gui-apply-dark-theme` |
| `raygui/icons` | icon enum constants and `gui-draw-icon` |

### Text buffers

`gui-text-box` edits text in place through a caller-allocated buffer from
`make-text-buf`. The buffer is a `cstr` handle; free it with
`free-text-buf` when the control is no longer needed. Read the current
text with `text-buf->cstr`.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/raygui>
