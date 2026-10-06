# tur-sdf-raylib

SDF-based solid modeling with raylib rendering and colored mesh export.
Build signed-distance-field scenes, extract meshes via marching cubes,
export to STL/OBJ/glTF, and preview live in a raylib window.

## Overview

`tur-sdf-raylib` is a Tier 3 spice (`cmake-dep` -- pulls in `raylib 5.5`
via `tur fetch`). It depends on `tur-math` for vector math. The spice
exposes a constructive-solid-geometry pipeline:

1. **SDF construction** -- primitive shapes (sphere, box, cylinder,
   plane, torus, capsule) composed with boolean ops (union, intersection,
   difference, smooth union/intersection) and transforms (translate,
   offset, shell, repeat).
2. **Evaluation** -- `sdf-eval` samples the distance field at a point;
   `sdf-normal-*` computes the surface normal.
3. **Mesh extraction** -- `mc-extract` runs marching cubes on a bounded
   box; `mc-extract-colored` / `dc-extract-colored` carry per-vertex
   colors and object IDs.
4. **Export** -- `export-stl`, `export-obj`, `export-glb` write the
   extracted mesh to disk.
5. **Preview** -- `sdf-preview-window` opens a raylib window with orbit
   controls; `csdf-preview-window` does the same for colored scenes.
6. **GLSL codegen** -- `csdf->glsl` compiles a colored SDF tree into a
   GLSL fragment shader string for GPU-side rendering.

## Install

```turmeric no-check
:spices {
  "sdf-raylib" {:url    "https://github.com/turmeric-lang/turmeric-spices"
                :ref    "sdf-raylib-v0.1.0"
                :subdir "spices/sdf-raylib"}
  "math" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "math-v0.1.0"
          :subdir "spices/math"}
}
```

## Quick start

```turmeric
(import sdf/primitives    :refer [sdf-sphere sdf-box])
(import sdf/boolean       :refer [sdf-difference sdf-union])
(import sdf/transforms    :refer [sdf-translate])
(import sdf/eval          :refer [sdf-eval])
(import sdf/expr          :refer [sdf-free])
(import mesh/marching-cubes :refer [mc-extract mesh-free])
(import export/stl        :refer [export-stl])
(import raylib/integration :refer [sdf-preview-window])

(let [body (sdf-sphere 0.0 0.0 0.0 1.5)
      hole (sdf-box -0.5 -0.5 -2.0 0.5 0.5 2.0)
      core (sdf-difference body hole)
      sat  (sdf-translate (sdf-sphere 0.0 0.0 0.0 0.45) 2.4 0.0 0.0)
      scene (sdf-union core sat)]
  (println "SDF at origin:" (sdf-eval scene 0.0 0.0 0.0))
  (let [mesh (mc-extract scene 32 -3.0 -3.0 -3.0 3.0 3.0 3.0)]
    (export-stl mesh "hello-sphere.stl")
    (mesh-free mesh))
  (sdf-preview-window scene 800 600 4 "Hello Sphere")
  (sdf-free scene))
```

```sweet-exp
#lang sweet-exp
import sdf/primitives    :refer [sdf-sphere sdf-box]
import sdf/boolean       :refer [sdf-difference sdf-union]
import sdf/transforms    :refer [sdf-translate]
import sdf/eval          :refer [sdf-eval]
import sdf/expr          :refer [sdf-free]
import mesh/marching-cubes :refer [mc-extract mesh-free]
import export/stl        :refer [export-stl]
import raylib/integration :refer [sdf-preview-window]

let [body sdf-sphere(0.0 0.0 0.0 1.5)
     hole sdf-box(-0.5 -0.5 -2.0 0.5 0.5 2.0)
     core sdf-difference(body hole)
     sat  sdf-translate(sdf-sphere(0.0 0.0 0.0 0.45) 2.4 0.0 0.0)
     scene sdf-union(core sat)]
  println $ "SDF at origin:" sdf-eval(scene 0.0 0.0 0.0)
  let [mesh mc-extract(scene 32 -3.0 -3.0 -3.0 3.0 3.0 3.0)]
    export-stl(mesh "hello-sphere.stl")
    mesh-free(mesh)
  sdf-preview-window(scene 800 600 4 "Hello Sphere")
  sdf-free(scene)
```

### Modules

| Module | Exports |
|--------|---------|
| `sdf/expr` | SDF tag constructors, accessors, `sdf-free` |
| `sdf/eval` | `sdf-eval`, `sdf-normal-*` |
| `sdf/primitives` | sphere, box, cylinder, plane, torus, capsule |
| `sdf/boolean` | union, intersection, difference, smooth union |
| `sdf/blend` | smooth intersection |
| `sdf/repeat` | infinite and finite repeat |
| `sdf/transforms` | translate, offset, shell |
| `sdf/colors` | colored SDF constructors and accessors |
| `glsl/codegen` | `csdf->glsl` -- compile colored SDF to GLSL |
| `mesh/marching-cubes` | `mc-extract`, vertex/index accessors, `mesh-free` |
| `mesh/extraction` | colored mesh extraction (marching cubes + dual contouring) |
| `mesh/optimization` | `cmesh-weld`, indexed mesh accessors |
| `export/stl` | `export-stl` |
| `export/obj` | `export-obj` |
| `export/gltf` | `export-glb` |
| `raylib/integration` | `sdf-preview-window`, `csdf-preview-window` |

### Resource management

SDF trees are heap-allocated handles. Call `sdf-free` (or `csdf-free`
for colored scenes) when the tree is no longer needed. Meshes from
`mc-extract` / `mc-extract-colored` must be freed with `mesh-free` /
`cmesh-free`. Welded meshes use `imesh-free`.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/sdf-raylib>
