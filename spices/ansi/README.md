# tur-ansi

Lightweight ncurses alternative: ANSI terminal control, raw-mode key input,
color, style, box drawing, and inline images (Kitty/iTerm2/sixel).

## Overview

`tur-ansi` is a Tier 1 spice (pure Turmeric + inline-C, no cmake deps). It
covers the full surface of VT100/ANSI escape sequences you need to build
interactive terminal UIs without pulling in ncurses or a comparable library.

The spice is split into focused modules so you only import what you use:

| Module | What it provides |
|---|---|
| `ansi/term` | Terminal setup, capability detection, raw-mode key reading, SIGWINCH |
| `ansi/color` | 4-bit (16), 8-bit (256), and 24-bit true-color SGR sequences |
| `ansi/style` | Text attributes: bold, italic, underline, blink, reverse, strikethrough |
| `ansi/cursor` | Absolute and relative cursor movement, show/hide, save/restore |
| `ansi/screen` | Screen and line clearing, alternate buffer, scrolling |
| `ansi/keys` | Key-name normalization, inverse mapping, modifier predicates |
| `ansi/box` | Unicode box-drawing glyphs, filled rectangles, titled boxes |
| `ansi/image` | Inline images: Kitty APC, iTerm2 OSC 1337, sixel, placeholder fallback |

## Install

```turmeric no-check
:spices {
  "ansi" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "ansi-v0.1.3"
          :subdir "spices/ansi"}
}
```

## Quick start

### Colored output

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

### Raw-mode key input

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

### Box drawing

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

## Module reference

### ansi/term

Terminal-wide setup and input. All other modules just emit escape sequences to
stdout; `ansi/term` owns the terminal state.

| Function | Signature | Description |
|---|---|---|
| `term-size` | `() -> int` | Returns `(cons rows cols)`; falls back to 24x80 |
| `term-color-support` | `() -> int` | `0`=none `1`=4-bit `2`=8-bit `3`=24-bit (env-detected) |
| `term-no-color?` | `() -> int` | `1` when `$NO_COLOR` is set and non-empty |
| `term-image-protocol` | `() -> int` | `0`=none `1`=kitty `2`=iterm2 `3`=sixel |
| `term-enable-raw` | `() -> void` | Enable raw mode + SIGWINCH self-pipe; idempotent |
| `term-disable-raw` | `() -> void` | Restore cooked mode and tear down self-pipe; idempotent |
| `term-read-key` | `() -> cstr` | Block until a keypress; returns a normalized key name |
| `term-read-key-timeout` | `(ms: int) -> cstr` | Like `term-read-key` but returns `""` after `ms` milliseconds |
| `term-on-resize` | `(cb: ptr<void>) -> void` | Register a `void(*)(void)` callback for SIGWINCH; pass `0` to clear |

Key names follow the Neovim/Helix convention: printable chars are themselves
(`"a"`, `"A"`, `" "`); non-printing keys are bracketed (`"<Enter>"`,
`"<Up>"`, `"<C-a>"`, `"<Resize>"`, `"<EOF>"`).

The environment variable `TUR_ANSI_TEST_KEYS` bypasses `select()` and replays
newline-delimited key names from its value, enabling scripted tests without a
real tty.

### ansi/color

SGR color sequences. Works in cooked or raw mode. The caller is responsible for
gating on `(term-no-color?)` when respecting the `$NO_COLOR` convention.

| Function | Signature | Description |
|---|---|---|
| `fg4` | `(idx: int) -> void` | 4-bit foreground (0-15); clamped |
| `bg4` | `(idx: int) -> void` | 4-bit background (0-15); clamped |
| `fg8` | `(idx: int) -> void` | 8-bit (256-color) foreground; clamped to [0,255] |
| `bg8` | `(idx: int) -> void` | 8-bit (256-color) background; clamped to [0,255] |
| `fg24` | `(r g b: int) -> void` | 24-bit true-color foreground |
| `bg24` | `(r g b: int) -> void` | 24-bit true-color background |
| `color-reset` | `() -> void` | SGR 39;49 -- reset fg+bg only (preserves bold/italic etc.) |

Named index constants (return `:int` for use with `fg4`/`bg4`):
`color-black`, `color-red`, `color-green`, `color-yellow`, `color-blue`,
`color-magenta`, `color-cyan`, `color-white`, `color-bright-black`,
`color-bright-red`, `color-bright-green`, `color-bright-yellow`,
`color-bright-blue`, `color-bright-magenta`, `color-bright-cyan`,
`color-bright-white`.

```turmeric
(fg4 (color-bright-red))   ;; same as (fg4 9)
```

#### `Color` typeclass (depth-typed `fg` / `bg`)

The depth-specific emitters above branch only on color *depth*, which the
caller already knows statically. The `Color` typeclass lifts depth into the
*type* of the color value, collapsing the six emitters to two methods, `fg`
and `bg`, dispatched on the value's type. Box a color with `color4` / `color8`
/ `rgb`, then call `fg` / `bg`:

| Constructor | Signature | Description |
|---|---|---|
| `color4` | `(idx: int) -> Color4` | box a 4-bit palette index (0-15) |
| `color8` | `(idx: int) -> Color8` | box an 8-bit palette index (0-255) |
| `rgb` | `(r g b: int) -> Color24` | box a 24-bit RGB triple |

| Method | Signature | Description |
|---|---|---|
| `fg` | `(c: a) -> void` where `(Color a)` | foreground SGR for the color value |
| `bg` | `(c: a) -> void` where `(Color a)` | background SGR for the color value |

```turmeric
(fg (color4 (color-red)))   ;; same bytes as (fg4 (color-red))
(fg (color8 208))           ;; same bytes as (fg8 208)
(bg (rgb 255 128 0))        ;; same bytes as (bg24 255 128 0)
```

Because depth is now a type, a routine parametric in a single `(Color a)`
can't be handed a color of the wrong depth -- e.g. one that only commits to
4-bit output won't silently accept a truecolor value.

### ansi/style

SGR attribute toggles. `style-reset` clears all attributes AND colors; use
`color-reset` when you only want to restore the default palette.

| Function | Signature | Description |
|---|---|---|
| `style-bold` | `() -> void` | SGR 1 |
| `style-dim` | `() -> void` | SGR 2 |
| `style-italic` | `() -> void` | SGR 3 |
| `style-underline` | `() -> void` | SGR 4 |
| `style-blink` | `() -> void` | SGR 5 (slow blink) |
| `style-blink-fast` | `() -> void` | SGR 6 (rapid blink; rarely supported) |
| `style-reverse` | `() -> void` | SGR 7 (inverse video) |
| `style-strikethrough` | `() -> void` | SGR 9 |
| `style-reset` | `() -> void` | SGR 0 -- clears all attributes and colors |

### ansi/cursor

VT100 cursor positioning. Works in cooked or raw mode.

| Function | Signature | Description |
|---|---|---|
| `cursor-move-to` | `(row col: int) -> void` | Absolute 1-based position (CSI H) |
| `cursor-up` | `(n: int) -> void` | Move up n rows |
| `cursor-down` | `(n: int) -> void` | Move down n rows |
| `cursor-left` | `(n: int) -> void` | Move left n columns |
| `cursor-right` | `(n: int) -> void` | Move right n columns |
| `cursor-col` | `(col: int) -> void` | Move to column on current row (CSI G) |
| `cursor-show` | `() -> void` | DECTCEM h -- make cursor visible |
| `cursor-hide` | `() -> void` | DECTCEM l -- hide cursor |
| `cursor-save` | `() -> void` | DEC SC (ESC 7) -- save position |
| `cursor-restore` | `() -> void` | DEC RC (ESC 8) -- restore saved position |

### ansi/screen

Screen and line clearing, alternate buffer, scrolling.

| Function | Signature | Description |
|---|---|---|
| `screen-clear` | `() -> void` | CSI 2J + CSI H -- clear and home cursor |
| `screen-clear-to-eol` | `() -> void` | CSI K -- erase from cursor to end of line |
| `screen-clear-line` | `() -> void` | CSI 2K -- erase entire current line |
| `alt-screen-enter` | `() -> void` | DECSET 1049 -- enter alternate screen buffer |
| `alt-screen-leave` | `() -> void` | DECRST 1049 -- return to primary buffer |
| `scroll-up` | `(n: int) -> void` | CSI S -- scroll viewport up n lines |
| `scroll-down` | `(n: int) -> void` | CSI T -- scroll viewport down n lines |

Always pair `alt-screen-enter` with `alt-screen-leave` at shutdown to preserve
the user's scrollback history.

### ansi/keys

Key-name strings, byte parsing, and modifier predicates.

| Function | Signature | Description |
|---|---|---|
| `parse-key-bytes` | `(bytes: cstr len: int) -> cstr` | Decode raw bytes into a normalized key name |
| `key-name->bytes` | `(name: cstr) -> cstr` | Inverse: produce the byte sequence a terminal would send |
| `key=?` | `(a b: cstr) -> int` | `1` when two key names are equal |
| `key-ctrl?` | `(k: cstr) -> int` | `1` when key carries a `C-` modifier |
| `key-shift?` | `(k: cstr) -> int` | `1` when key carries an `S-` modifier |
| `key-alt?` | `(k: cstr) -> int` | `1` when key carries an `M-` (Meta/Alt) modifier |

Key name format: printable characters are passed through verbatim; others use
angle-bracket notation with optional modifier prefixes in order C-, M-, S-.
Examples: `"a"`, `"<Enter>"`, `"<Up>"`, `"<C-a>"`, `"<S-Tab>"`,
`"<C-M-S-Right>"`. Unknown byte sequences are returned as `"<raw:HH...>"` so
all input remains printable.

`key-name->bytes` is the inverse and is mainly useful for writing scripted
tests via `TUR_ANSI_TEST_KEYS`.

### ansi/box

Unicode box-drawing characters and high-level rectangle helpers. Three border
styles are available:

| Style int | Appearance |
|---|---|
| `0` | Single line (`+--+`) |
| `1` | Double line (`+==+`) |
| `2` | Round corners (single lines with curved corners) |

| Function | Signature | Description |
|---|---|---|
| `box-draw` | `(row col height width style: int) -> void` | Draw a bordered rectangle; minimum 2x2 |
| `box-fill` | `(row col height width ch: int) -> void` | Fill the interior with ASCII char `ch`; call before `box-draw` |
| `box-title` | `(row col width: int title: cstr) -> void` | Write a centered title on the top border; call after `box-draw` |

Individual glyph accessors (return `:cstr`): `box-tl-single`, `box-tr-single`,
`box-bl-single`, `box-br-single`, `box-h-single`, `box-v-single`,
`box-tl-double`, `box-tr-double`, `box-bl-double`, `box-br-double`,
`box-h-double`, `box-v-double`, `box-tl-round`, `box-tr-round`,
`box-bl-round`, `box-br-round`.

Typical draw order: `box-fill` first (so the fill does not overwrite the
border), then `box-draw`, then `box-title`.

### ansi/image

Inline image display using the protocol best suited to the running terminal.

| Function | Signature | Description |
|---|---|---|
| `image-display` | `(path: cstr) -> void` | Display a PNG file using the best detected protocol |
| `image-display-protocol` | `(proto: int path: cstr) -> void` | Display with an explicit protocol (0-3) |
| `image-display-base64` | `(b64: cstr) -> void` | Emit a Kitty APC sequence from base64-encoded PNG data |
| `image-display-rgba` | `(w h: int rgba: cstr) -> void` | Emit a sixel sequence from a raw RGBA pixel buffer |
| `image-placeholder` | `(path: cstr) -> void` | Emit `[image: path]` as a text fallback |

Protocol codes match `(term-image-protocol)`: `0`=none/placeholder, `1`=Kitty,
`2`=iTerm2, `3`=sixel.

`image-display-rgba` uses a 6x6x6 color-cube quantizer (216 colors) and emits
run-length-encoded sixel bands. The caller must decode any compressed image
format (e.g. PNG via `tur-png`) into a flat RGBA byte buffer before calling.
Sixel support in `image-display` and `image-display-protocol` is deferred to
v0.2; those paths fall back to the placeholder for `proto=3`.

## Examples

The `examples/` directory has runnable demos:

| File | What it shows |
|---|---|
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

## Testing

```sh
just test
```

Tests use `TUR_ANSI_TEST_KEYS` to feed scripted key sequences without a real
tty, so the key-reading and key-name test suites run safely in CI.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/ansi-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/ansi>
