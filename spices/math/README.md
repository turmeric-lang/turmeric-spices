# tur-math

2D/3D vector and matrix math for Turmeric: `vec2`, `vec3`, `vec4`, `mat4`,
quaternions, and common scalar helpers.

## Overview

`tur-math` is a Tier 1 spice (pure Turmeric, no C deps). It exposes the
linear-algebra primitives you typically need for graphics, physics, and
geometry: vectors with arithmetic and normalization, 4x4 transform matrices,
and unit quaternions with slerp.

The implementation is deliberately small and allocation-light so it can be
used alongside `tur-opengl`, `tur-raylib`, and the GLSL DSL without surprises.

## Install

```turmeric no-check
:spices {
  "math" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "math-v0.1.0"
          :subdir "spices/math"}
}
```

## Quick start

```turmeric
(import math/vec3 :refer [vec3 v3-cross])

(let [a (vec3 1.0 0.0 0.0)
      b (vec3 0.0 1.0 0.0)]
  (println (v3-cross a b)))  ; => (0.0 0.0 1.0)
```

```sweet-exp
#lang sweet-exp
import math/vec3 :refer [vec3 v3-cross]

let [a vec3(1.0 0.0 0.0)
     b vec3(0.0 1.0 0.0)]
  println $ v3-cross a b
```

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/math-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/math>
