---
title: ANSI Terminal Control
category: Terminal and TUI
description: Colors, cursor movement, raw-mode input, box drawing, and inline images -- a lightweight ncurses alternative
audience: developers building interactive terminal UIs, CLI tools, or TUI applications
since: ansi v0.1.3
---

# tur-ansi Guide

`tur-ansi` is a lightweight ncurses alternative: ANSI terminal control, raw-mode
key input, color, style, box drawing, and inline images. It covers the full
surface of VT100/ANSI escape sequences you need to build interactive terminal
UIs without pulling in ncurses or a comparable library.

This guide walks the six things you'll do most often:

1. [Colored and styled output](#1-colored-and-styled-output)
2. [Raw-mode key input](#2-raw-mode-key-input)
3. [Cursor movement and screen control](#3-cursor-and-screen)
4. [Box drawing](#4-box-drawing)
5. [Inline images](#5-inline-images)
6. [Building a TUI event loop](#6-building-a-tui-event-loop)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "ansi" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "ansi-v0.1.3"
          :subdir "spices/ansi"}
}
```

Then `tur fetch`. No CMake dependency -- `tur-ansi` is pure Turmeric with
inline-C escape-sequence emitters.

---

## 1. Colored and styled output

The simplest use case: emit SGR (Select Graphic Rendition) sequences to
stdout. Works in cooked mode -- no raw-mode setup needed.

```turmeric
(import ansi/color :refer [fg24 bg24 color-reset])
(import ansi/style :refer [style-bold style-reset])

(style-bold)
(fg24 255 200 80)
(bg24 20 20 40)
(println "hello, tur-ansi")
(style-reset)
(color-reset)
```

```sweet-exp
#lang sweet-exp
import ansi/color :refer [fg24 bg24 color-reset]
import ansi/style :refer [style-bold style-reset]

style-bold()
fg24(255 200 80)
bg24(20 20 40)
println("hello, tur-ansi")
style-reset()
color-reset()
```

### Color depth

Three depths are available, and the right one depends on the terminal:

| Functions | Depth | Example |
|-----------|-------|---------|
| `fg4` / `bg4` | 4-bit (16 colors) | `(fg4 (color-red))` |
| `fg8` / `bg8` | 8-bit (256 colors) | `(fg8 208)` |
| `fg24` / `bg24` | 24-bit truecolor | `(fg24 255 128 0)` |

Detect what the terminal supports with `term-color-support`:

```turmeric
(import ansi/term :refer [term-color-support])

(let [depth (term-color-support)]
  (if (= depth 3)
    (fg24 255 128 0)       ;; truecolor
    (if (= depth 2)
      (fg8 208)            ;; 256-color
      (fg4 (color-red))))) ;; 16-color fallback
```

### The Color typeclass

The depth-specific emitters branch only on color *depth*, which the caller
already knows statically. The `Color` typeclass lifts depth into the *type*
of the color value, collapsing six emitters to two methods:

```turmeric
(import ansi/color :refer [color4 color8 rgb fg bg Color])

(fg (color4 (color-red)))    ;; same bytes as (fg4 (color-red))
(fg (color8 208))            ;; same bytes as (fg8 208)
(bg (rgb 255 128 0))         ;; same bytes as (bg24 255 128 0)
```

Because depth is now a type, a routine parametric in a single `(Color a)`
can't be handed a color of the wrong depth.

### Style attributes

```turmeric
(import ansi/style :refer [style-bold style-italic style-underline
                           style-reverse style-strikethrough style-reset])

(style-bold)
(style-underline)
(println "important text")
(style-reset)   ;; clears all attributes AND colors
```

`style-reset` (SGR 0) clears everything. Use `color-reset` (SGR 39;49) when
you only want to restore the default palette and keep bold/italic/etc.

### Respecting NO_COLOR

```turmeric
(import ansi/term :refer [term-no-color?])

(if (= (term-no-color?) 0)
  (fg24 255 200 80)    ;; color is fine
  (println))           ;; NO_COLOR is set -- skip color
```

---

## 2. Raw-mode key input

`ansi/term` owns the terminal state. Enable raw mode to read individual
keypresses without line buffering, then disable it on exit.

```turmeric
(import ansi/term :refer [term-enable-raw term-disable-raw term-read-key])
(import ansi/keys :refer [key=?])

(term-enable-raw)
(let [k (term-read-key)]
  (if (= (key=? k "<C-c>") 1)
    (println "quit!")
    (println k)))
(term-disable-raw)
```

```sweet-exp
#lang sweet-exp
import ansi/term :refer [term-enable-raw term-disable-raw term-read-key]
import ansi/keys :refer [key=?]

term-enable-raw()
let [k term-read-key()]
  if =(key=?(k "<C-c>") 1)
    println("quit!")
    println(k)
term-disable-raw()
```

### Key name format

Key names follow the Neovim/Helix convention:

| Key | Name |
|-----|------|
| `a` | `"a"` |
| Enter | `"<Enter>"` |
| Up arrow | `"<Up>"` |
| Ctrl+A | `"<C-a>"` |
| Shift+Tab | `"<S-Tab>"` |
| Ctrl+Alt+Shift+Right | `"<C-M-S-Right>"` |

Modifier prefixes are always in order `C-`, `M-`, `S-`. Unknown byte
sequences are returned as `"<raw:HH...>"` so all input remains printable.

### Timeout reads

`term-read-key-timeout` returns `""` after `ms` milliseconds with no input.
Use it for event loops that need to do work between key checks:

```turmeric
(import ansi/term :refer [term-read-key-timeout])

(let [k (term-read-key-timeout 100)]
  (if (= k "")
    (do-background-work)
    (handle-key k)))
```

### Scripted testing

The environment variable `TUR_ANSI_TEST_KEYS` bypasses `select()` and
replays newline-delimited key names from its value, enabling scripted tests
without a real tty:

```sh
TUR_ANSI_TEST_KEYS="<Enter>\n<C-c>" tur run my-app.tur
```

`key-name->bytes` is the inverse of `parse-key-bytes` and is mainly useful
for generating these test inputs.

---

## 3. Cursor and screen

### Cursor movement

```turmeric
(import ansi/cursor :refer [cursor-move-to cursor-up cursor-down
                             cursor-left cursor-right
                             cursor-show cursor-hide
                             cursor-save cursor-restore])

(cursor-move-to 5 10)    ;; row 5, column 10 (1-based)
(cursor-hide)
(println "invisible cursor")
(cursor-show)
(cursor-save)
(cursor-move-to 1 1)
(cursor-restore)         ;; back to where we saved
```

### Screen control

```turmeric
(import ansi/screen :refer [screen-clear screen-clear-to-eol
                             alt-screen-enter alt-screen-leave
                             scroll-up scroll-down])

(alt-screen-enter)       ;; switch to alternate buffer (preserves scrollback)
(screen-clear)
;; ... draw your TUI ...
(alt-screen-leave)       ;; restore primary buffer on exit
```

Always pair `alt-screen-enter` with `alt-screen-leave` at shutdown to
preserve the user's scrollback history.

### Terminal size and resize

```turmeric
(import ansi/term :refer [term-size term-on-resize])

(let [size (term-size)]
  (let [rows (cons-first size)
        cols (cons-second size)]
    (println "terminal is" cols "x" rows)))

;; Register a callback for SIGWINCH (terminal resize)
(term-on-resize (cast (fn [] (println "resized!"))))
```

`term-size` returns `(cons rows cols)`, falling back to 24x80 if detection
fails. Pass `0` to `term-on-resize` to clear the callback.

---

## 4. Box drawing

Unicode box-drawing characters and high-level rectangle helpers. Three
border styles:

| Style | Appearance |
|-------|------------|
| `0` | Single line (`+--+`) |
| `1` | Double line (`+==+`) |
| `2` | Round corners |

```turmeric
(import ansi/screen :refer [screen-clear])
(import ansi/box    :refer [box-fill box-draw box-title])

(screen-clear)
(box-fill  2 2 10 40 32)          ;; clear interior with spaces
(box-draw  2 2 10 40 2)           ;; style 2 = round corners
(box-title 2 2 40 " my panel ")
```

```sweet-exp
#lang sweet-exp
import ansi/screen :refer [screen-clear]
import ansi/box    :refer [box-fill box-draw box-title]

screen-clear()
box-fill(2 2 10 40 32)
box-draw(2 2 10 40 2)
box-title(2 2 40 " my panel ")
```

Typical draw order: `box-fill` first (so the fill does not overwrite the
border), then `box-draw`, then `box-title`.

Individual glyph accessors (return `:cstr`) are available for custom
layouts: `box-tl-single`, `box-tr-single`, `box-bl-single`, `box-br-single`,
`box-h-single`, `box-v-single`, and the double/round variants.

---

## 5. Inline images

Display images inline using the protocol best suited to the running
terminal. Detection is automatic.

```turmeric
(import ansi/term  :refer [term-image-protocol])
(import ansi/image :refer [image-display image-display-rgba])

;; Display a PNG file -- protocol is auto-detected
(image-display "logo.png")

;; Or use an explicit protocol (0=placeholder, 1=kitty, 2=iterm2, 3=sixel)
(image-display-protocol 1 "logo.png")

;; Display raw RGBA pixels as a sixel
(image-display-rgba 100 100 rgba-buffer)
```

Protocol codes match `term-image-protocol`: `0`=none/placeholder, `1`=Kitty
APC, `2`=iTerm2 OSC 1337, `3`=sixel.

`image-display-rgba` uses a 6x6x6 color-cube quantizer (216 colors) and
emits run-length-encoded sixel bands. The caller must decode any compressed
image format (e.g. PNG via `tur-png`) into a flat RGBA byte buffer before
calling.

---

## 6. Building a TUI event loop

Putting it all together: a minimal TUI that reads keys, redraws on resize,
and exits on Ctrl-C or `q`.

```turmeric
(defmodule tui
  (import ansi/term   :refer [term-enable-raw term-disable-raw
                              term-read-key-timeout term-size
                              term-on-resize])
  (import ansi/screen :refer [screen-clear alt-screen-enter alt-screen-leave])
  (import ansi/cursor :refer [cursor-move-to cursor-hide cursor-show])
  (import ansi/keys    :refer [key=?])
  (import ansi/box     :refer [box-draw box-title])
  (import ansi/color   :refer [fg24 color-reset])
  (import ansi/style   :refer [style-bold style-reset])

  (defn draw-frame [rows : int cols : int] : void
    (screen-clear)
    (box-draw 1 1 (- rows 1) cols 0)
    (box-title 1 1 cols " tur-ansi TUI ")
    (cursor-move-to 3 3)
    (style-bold)
    (fg24 255 200 80)
    (println "Press q or Ctrl-C to quit")
    (style-reset)
    (color-reset))

  (defn loop [] : int
    (let [size (term-size)]
      (draw-frame (cons-first size) (cons-second size)))
    (let [k (term-read-key-timeout 100)]
      (if (or (= (key=? k "q") 1) (= (key=? k "<C-c>") 1))
        0
        (do
          (if (= k "<Resize>")
            (let [size (term-size)]
              (draw-frame (cons-first size) (cons-second size))))
          (loop))))

  (defn main [] : int
    (term-enable-raw)
    (cursor-hide)
    (alt-screen-enter)
    (let [rc (loop)]
      (alt-screen-leave)
      (cursor-show)
      (term-disable-raw)
      rc))
)
```

```sweet-exp
#lang sweet-exp
defmodule tui
  import ansi/term   :refer [term-enable-raw term-disable-raw
                              term-read-key-timeout term-size
                              term-on-resize]
  import ansi/screen :refer [screen-clear alt-screen-enter alt-screen-leave]
  import ansi/cursor :refer [cursor-move-to cursor-hide cursor-show]
  import ansi/keys    :refer [key=?]
  import ansi/box     :refer [box-draw box-title]
  import ansi/color   :refer [fg24 color-reset]
  import ansi/style   :refer [style-bold style-reset]

  defn draw-frame [rows :int cols :int] :void
    screen-clear()
    box-draw(1 1 (- rows 1) cols 0)
    box-title(1 1 cols " tur-ansi TUI ")
    cursor-move-to(3 3)
    style-bold()
    fg24(255 200 80)
    println("Press q or Ctrl-C to quit")
    style-reset()
    color-reset()

  defn loop [] :int
    let [size term-size()]
      draw-frame(cons-first size cons-second size)
    let [k term-read-key-timeout(100)]
      if or(=(key=?(k "q") 1) =(key=?(k "<C-c>") 1))
        0
        do
          if =(k "<Resize>")
            let [size term-size()]
              draw-frame(cons-first size cons-second size)
          loop()

  defn main [] :int
    term-enable-raw()
    cursor-hide()
    alt-screen-enter()
    let [rc loop()]
      alt-screen-leave()
      cursor-show()
      term-disable-raw()
      rc
```

Key patterns in this loop:

- **Timeout-based polling** (`term-read-key-timeout 100`) lets the loop do
  background work between key checks.
- **Resize handling** checks for `"<Resize>"` and redraws the full frame.
- **Clean shutdown** always restores the terminal state, even on early exit.
  In production code, wrap the body in a `try`/`finally` to guarantee
  `term-disable-raw` + `alt-screen-leave` + `cursor-show` run on any path.

---

## Examples

The `examples/` directory has runnable demos:

| File | What it shows |
|------|---------------|
| `examples/hello-color.tur` | Capability detection, 256-color gradient, truecolor banner |
| `examples/keys-dump.tur` | Raw-mode key loop printing normalized key names as you type |
| `examples/box-demo.tur` | Three nested boxes (single/double/round) with titles |
| `examples/image-demo.tur` | Protocol detection and `image-display` / `image-display-base64` |

```sh
tur run examples/hello-color.tur
tur run examples/keys-dump.tur       # exit: Ctrl-C, Ctrl-D, or Esc Esc
tur run examples/box-demo.tur
IMAGE_DEMO_PATH=logo.png tur run examples/image-demo.tur
```

---

## Integration with other spices

- **`tur-notebook`**: notebook uses `ansi/image` for inline image display
  and `ansi/term` for terminal capability detection. The composition is
  documented in the [notebook guide](notebook-guide.md).
- **`tur-png`**: decode a PNG into an RGBA buffer, then hand it to
  `image-display-rgba` for sixel rendering.
- **`tur-watch`**: pair `tur-ansi` with a file watcher for a live-updating
  TUI that redraws when files change.

---

## See also

- [API reference](api/)
- [README](../../spices/ansi/README.md) -- full module reference tables
- [Notebook guide](notebook-guide.md) -- ansi + png + watch composition
