# tur-notebook -- Notebook Guide

> Spice version 0.1.0 -- Literate `.tur.md` notebooks with a static renderer
> and an interactive terminal TUI.
> Audience: Turmeric users who want a Jupyter-style workflow for exploratory
> code, reproducible analyses, and shareable HTML reports.
>
> A `.tur.md` file is a strict superset of CommonMark: ordinary markdown that
> renders cleanly in GitHub / VS Code / Obsidian / pandoc, where fenced code
> blocks tagged `turmeric` or `sweet-exp` are executable cells. Pair with
> [`tur-frame`](frame-guide.md) for data loading, [`tur-plot`](https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/plot)
> for figures, or [`tur-stats`](https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/stats) for analysis.

This guide walks through the workflow:

1. [Writing your first `.tur.md`](#1-your-first-notebook)
2. [Using external spices from cells](#2-using-external-spices-from-cells)
3. [Rendering: markdown vs HTML, watch mode](#3-rendering)
4. [The TUI: command mode and editing](#4-the-tui)
5. [Caching expensive cells](#5-caching)
6. [Embedding plots and images](#6-plots-and-images)
7. [Data analysis workflows](#7-data-analysis-workflows)
8. [Reproducibility and CI](#8-reproducibility-and-ci)
9. [Customizing keybindings](#9-customizing-keybindings)

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "notebook" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "notebook-v0.1.0"
               :subdir "spices/notebook"}
}
```

Then:

```sh
tur fetch
tur install tur-notebook    ;; puts `tur-nb` on $PATH
```

No external C dependencies -- the parser, renderer, and TUI are pure Turmeric
plus a vendored libturi for in-process cell execution.

---

## 1. Your first notebook

Scaffold a starter file:

```sh
tur nb new analysis.tur.md
```

Open it in any editor. The body looks like ordinary markdown -- prose, headings,
lists -- except that fenced `turmeric` blocks are *cells*:

````markdown
# My Analysis

Some prose explaining what we're about to do.

```turmeric
(+ 1 2)
```

More prose.

```turmeric {id=greeting}
(println "hello, notebook")
```
````

Cells have optional attributes inside `{...}` on the fence line (Quarto
style). The most common:

| Attribute | Default | Meaning |
|-----------|---------|---------|
| `id`      | auto (`cell-1`, `cell-2`, ...) | Stable handle for `--cell` and TUI navigation |
| `eval`    | `true`  | Set `false` to render without executing |
| `echo`    | `true`  | Set `false` to hide the source in rendered output |
| `output`  | `true`  | Set `false` to suppress the output block |
| `error`   | `halt`  | `continue` records the error and proceeds |
| `cache`   | `false` | Cache by source hash under `.turnb-cache/` |
| `depends` | (none)  | Comma-separated cell ids this cell depends on |
| `image`   | `inline`| `inline` = base64 in rendered file; `file` = sibling PNG |

Cells tagged `sweet-exp` use [sweet-expression syntax](#) -- everything else
about the workflow is identical.

---

## 2. Using external spices from cells

All cells in a notebook share one libturi session, so an `(import ...)`
in one cell makes the module available in every subsequent cell -- exactly
like a Jupyter kernel. This is what makes cross-spice workflows possible:
load data with `tur-frame` in one cell, fit a model with `tur-stats` in
the next, plot the result with `tur-plot` in a third.

The spices most useful in notebooks:

| Spice | Import | What it provides |
|-------|--------|------------------|
| `plot`   | `(import plot/core :refer [plot-write-png])`   | 2D visualization, PNG output |
| `linalg` | `(import linalg/mat :refer [mat-of mat-mul])`  | Dense linear algebra |
| `stats`  | `(import stats/dist :refer [dnorm pnorm])`    | Distributions, tests, regression |
| `frame`  | `(import frame/csv :refer [read-csv])`        | Data frames, CSV I/O |

These spices must be declared in your project's `build.tur` (or the
notebook's own `build.tur` as `:optional true` deps) so `tur fetch` makes
them available. Without the declaration, the import fails with a clear
error -- the spice is not installed.

Imports resolve the way `tur run <notebook>` would for a program at the
notebook's path: first the notebook's own directory (a `helpers.tur` next to
it is `(import helpers ...)`), then the enclosing spice's `src/`, each
`:spices` dep's `src/`, and the other members of the workspace -- whatever
directory you render from. The session starts from the same stdlib
`tur --interpret` gives a program.

**Current limit:** cells run in the interpreter, which does not run inline-C
bodies. A spice whose functions are written in C -- most of `plot`, `stats`,
`frame` and `linalg`'s solvers and formatter -- imports fine, but the first
call into such a function reports `inline-C not supported in interpreter
mode`. Pure-Turmeric code (your own `defn`s, `linalg/mat`) runs. Tracked as
turmeric's `docs/reported/notebook-cells-cannot-call-inline-c-spices.md`.

Example -- load a CSV and print its shape:

```turmeric
(import frame/csv   :refer [read-csv-string])
(import frame/frame  :refer [frame-nrows frame-ncols])
(import frame/print  :refer [print-frame])

(def df (read-csv-string "x,y\n1,2\n2,4\n3,6\n" 0 0 1 0 ""))
(println (str-append "rows: " (int->str (frame-nrows df))))
(print-frame (frame-head df 3))
```

See the [frame guide](frame-guide.md) for the full `tur-frame` API, the
[linalg guide](linalg-guide.md) for linear algebra, and the example
notebooks in `spices/notebook/examples/` for end-to-end workflows.

---

## 3. Rendering

Render to markdown (the default):

```sh
tur nb render analysis.tur.md            # writes analysis.md
```

Render to a standalone HTML page:

```sh
tur nb render analysis.tur.md --to html  # writes analysis.html
```

`tur nb export` is a more discoverable alias for the same workflow:

```sh
tur nb export md   analysis.tur.md
tur nb export html analysis.tur.md
tur nb export html analysis.tur.md --out site/    # write into a directory
tur nb export md   analysis.tur.md --no-output    # strip output blocks
tur nb export md   analysis.tur.md --no-source    # outputs only
```

For "I'm writing prose, just keep the rendered file fresh," watch mode
re-renders on every save:

```sh
tur nb render analysis.tur.md --watch
```

Watch mode uses `kqueue` on macOS and `inotify` on Linux. Each re-render
starts with a fresh session -- predictability over warm caches. If you want
warm-cache exploration, use the TUI instead.

---

## 4. The TUI

```sh
tur nb tui analysis.tur.md
```

The TUI is **modal**, in the Jupyter / vim style. In *command mode*, single
keys navigate and re-run cells:

| Key | Action |
|-----|--------|
| `j` / `k`        | Move focus down / up |
| `gg` / `G`       | Jump to first / last cell |
| `Enter`          | Re-run the focused cell |
| `Shift-Enter`    | Run focused cell, then move to the next |
| `R`              | Restart session and re-run all |
| `r`              | Re-run from the focused cell onward |
| `e`              | Edit the focused cell (`$EDITOR`) |
| `a` / `b`        | Insert a new cell above / below |
| `dd` / `p`       | Delete (yank) / paste a cell |
| `o`              | Toggle output visibility |
| `s`              | Save the file |
| `/`, `n`, `N`    | Search across cell sources and outputs |
| `?`              | Help overlay |
| `q`              | Quit (prompts if dirty) |

Hitting `e` writes the focused cell to a temp file and spawns `$EDITOR` on
it; on exit, the cell source is replaced and the file marked dirty. The TUI
does **not** ship its own text editor -- you get the keybindings, theme, and
plugins you have already configured for vim / helix / nano / emacs / VS Code.

The interpreter session lives for the lifetime of the TUI process: definitions
made in one cell are visible in later ones, exactly like a Jupyter kernel.
`R` is the "clean slate" key when you want to verify a notebook runs from a
fresh session.

---

## 5. Caching

For cells that are slow to recompute (loading a large CSV, fitting a model),
opt in to source-hash caching:

````markdown
```turmeric {id=load-data cache=true}
(import frame/csv :refer [read-csv default-csv-opts])
(def iris (read-csv "iris.csv" (default-csv-opts)))
```

```turmeric {id=fit-model cache=true depends=load-data}
(def model (fit iris))
```
````

The cache key is `SHA-256(cell-source + sorted-attrs + dependency-hashes)`.
Editing `load-data` busts every downstream cell that lists it in `depends`.
Pass `--cache` to enable the cache for `render` / `export`:

```sh
tur nb render analysis.tur.md --cache
```

The cache lives in `.turnb-cache/` beside the source file -- add it to
`.gitignore`.

---

## 6. Plots and images

`tur-notebook` does not require any plotting library. To embed an image, a
cell writes a PNG and announces its path via the image hook:

```turmeric
(import notebook/image :refer [image-hook-record-path])
(import plot/core      :refer [plot-write-png])

(plot-write-png renderers opts "iris-scatter.png")
(image-hook-record-path "iris-scatter.png")
```

The hook works via a stdout marker (`__NB_IMG__: <path>`) that the session
intercepts before display. Cells get the path back as part of their
`cell-output.image-paths` list, and the TUI / renderers do the right thing
with it:

- **HTML render**: the PNG is embedded inline as a base64 data URL (or as
  a sibling-file `<img src=...>` link when `image=file` is set on the cell).
- **Markdown render**: the PNG is emitted as a `![](data:image/png;base64,...)`
  tag, so the rendered `.md` is self-contained.
- **TUI**: detects terminal support and uses the
  [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/)
  or the [iTerm2 inline image protocol](https://iterm2.com/documentation-images.html)
  to draw the image directly in the output region. Terminals without
  image support get a `[image: path]` placeholder; the file is still on
  disk and openable from the shell.

This is the same opt-in pattern `tur-plot` and `tur-plutovg` use: cells that
write PNGs can advertise them, but the spices themselves stay independent of
the notebook tooling.

---

## 7. Data analysis workflows

The notebook format shines when a workflow spans multiple spices. Here is
a condensed end-to-end analysis: load a CSV with `tur-frame`, compute
summary statistics with `tur-stats`, fit a regression, and plot the data
and the fitted line with `tur-plot`. The full version lives in
`spices/notebook/examples/data-analysis.tur.md`.

Load the data:

```turmeric
(import frame/csv   :refer [read-csv-string])
(import frame/frame  :refer [frame-nrows frame-head])
(import frame/print  :refer [print-frame])

(def csv "x,y\n1,2.1\n2,3.9\n3,6.2\n4,8.1\n5,9.8\n")
(def df (read-csv-string csv 0 0 1 0 ""))
(print-frame (frame-head df 5))
```

Summarize and fit:

```turmeric
(import frame/frame    :refer [frame-column])
(import stats/summary  :refer [col-mean col-sd])
(import stats/regress   :refer [ols-frame])
(import stats/fmt      :refer [print-fit])

(def y-col (frame-column df "y"))
(println (str-append "mean = " (float->str (col-mean y-col))))
(println (str-append "sd   = " (float->str (col-sd y-col))))

(def fit (ols-frame df "y" (cons (cast "x" :int) 0) 1))
(print-fit fit)
```

Plot the data and the fitted line (see [Plots and images](#6-plots-and-images)
for the two-call pattern):

```turmeric
(import plot/core  :refer [plot-write-png])
(import plot/point :refer [points])
(import plot/line  :refer [function])
(import plot/decor :refer [axes tick-grid])
(import plot/style :refer [default-line-style default-point-style default-plot-opts])
(import notebook/image :refer [image-hook-record-path])

;; ... build scatter data from the frame, then:
(plot-write-png
  (vec-of (tick-grid)
          (axes)
          (points scatter-data (default-point-style) "observed")
          (function fit-line 0.0 10.0 128
                    (default-line-style) "fitted"))
  (default-plot-opts)
  "/tmp/nb-analysis.png")
(image-hook-record-path "/tmp/nb-analysis.png")
```

Each step is a cell, the prose explains the analysis, and `tur nb export html`
produces a shareable report with the plot embedded as a base64 data URL.

---

## 8. Reproducibility and CI

Notebooks that use randomness (any cell calling into `tur-stats`'s `rng-*`
or any PRNG) should pass an **explicit seed**. The notebook tooling does
*not* auto-seed -- doing so would make notebooks that look reproducible
silently non-reproducible the moment they are edited. Spell the seed in
user code:

```turmeric
(import stats/rng :refer [rng-make])
(def rng (rng-make 42))
```

For CI, the `exec` subcommand runs cells without writing output blocks back
to disk:

```sh
tur nb exec analysis.tur.md --all              # run every cell, print outputs
tur nb exec analysis.tur.md --cell fit-model   # run from this cell onward
```

A non-zero exit code means at least one cell errored, so this composes
cleanly with `set -e` in a CI script:

```sh
#!/bin/sh
set -e
for nb in notebooks/*.tur.md; do
  tur nb exec "$nb" --all >/dev/null
done
```

Combine with deterministic seeds and you can diff notebook outputs in version
control to catch regressions in numerical behavior.

---

## 9. Customizing keybindings

The TUI's defaults live in `notebook/keys.tur`. Override them with a
file passed to `--keybindings`:

```
# ~/.turnb-keys
# one "key action" per line; # starts a comment
j        cell-next
k        cell-prev
<Enter>  run-cell
e        edit-cell
R        restart-and-run-all
q        quit
```

```sh
tur nb tui analysis.tur.md --keybindings ~/.turnb-keys
```

User bindings are merged onto the built-in defaults; only the actions you
list are overridden, so a minimal file is fine. The available actions are
documented in `notebook/keys.tur` (`default-keybindings`).

`--no-color` disables ANSI colors entirely -- useful for terminals that do
not handle 256-color escapes well, or for screen-readers.

---

## Limitations in v0.1.0

- **No undo for cell-level edits.** Insert / delete / paste are not yet
  reversible from inside the TUI. The save-on-quit prompt protects against
  accidental loss; full undo is planned for v0.2.
- **HTML blocks and reference-style links** are not parsed by the included
  CommonMark subset; they render as their literal source text. Inline links,
  GFM tables, task lists, and strikethrough are supported.
- **Terminal image protocols vary.** Inline images work in Kitty, iTerm2,
  and WezTerm; other terminals fall back to a `[image: path]` placeholder
  in the TUI. Rendered HTML and markdown carry the image regardless.

The parser scope and the included / deferred features are documented in
`docs/notebook-spice-plan.md` -- file an issue if a missing CommonMark feature
is blocking real notebook work.
