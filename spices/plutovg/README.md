# tur-plutovg

2D vector graphics rendering via plutovg: paths, fills, strokes, gradients,
textures, text, and PNG export.

## Overview

`tur-plutovg` is a Tier 2 spice (`cmake-dep` -- pulls in `plutovg 1.3` via
`tur fetch`). It exposes the plutovg surface for off-screen 2D rendering:
canvases, paths, paints (solid / gradient / pattern), font loading, and
direct PNG output.

This is the same engine that backs LunaSVG / PlutoSVG, so it is a natural
fit for SVG-style rendering pipelines, icon generation, plotting, and
print-quality output. Pairs with `tur-png` when you need pixel-level access
to the rasterized result.

## Install

```turmeric no-check
:spices {
  "plutovg" {:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "plutovg-v0.1.0"
             :subdir "spices/plutovg"}
}
```

## Quick start

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

```sweet-exp
#lang sweet-exp
import plutovg/surface :refer [surface-create surface-write-png surface-destroy]
import plutovg/canvas  :refer [canvas-create canvas-destroy
                                canvas-set-source-color
                                canvas-circle canvas-fill]

let [s ok-val(surface-create(256 256))
     c ok-val(canvas-create(s))]
  canvas-set-source-color(c 0.9 0.2 0.2 1.0)
  canvas-circle(c 128.0 128.0 96.0)
  canvas-fill(c)
  surface-write-png(s "circle.png")
  canvas-destroy(c)
  surface-destroy(s)
```

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/plutovg-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/plutovg>
