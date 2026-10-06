---
title: Vector and Matrix Math
category: Math
description: 2D/3D vectors, 4x4 transform matrices, quaternions with slerp, and scalar helpers -- pure Turmeric, no C deps
audience: developers building graphics, physics, or geometry in Turmeric
since: math v0.1.0
---

# tur-math Guide

Graphics, physics, and geometry all need the same primitives: vectors you
can add and normalize, 4x4 matrices you can multiply and invert, and
quaternions you can slerp between. This spice provides them in a small,
allocation-light surface that pairs with `tur-opengl`, `tur-raylib`, and
the GLSL DSL without surprises.

This guide walks the four things you will do most often:

1. [Scalar helpers: clamp, lerp, remap](#1-scalars)
2. [Vectors: vec2, vec3, vec4](#2-vectors)
3. [Matrices: transforms, projection, look-at](#3-matrices)
4. [Quaternions: axis-angle, slerp, to-mat4](#4-quaternions)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "math" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "math-v0.1.0"
           :subdir "spices/math"}
}
```

Then `tur fetch`. Pure Turmeric -- no C deps. The implementation uses
inline-C blocks for the arithmetic (so it links libm where needed), but
nothing is fetched or built externally.

---

## The one idea

Every vector and matrix is a heap-allocated block returned as an `:int`
handle. Operations are functional: each returns a fresh value rather than
mutating in place. There is no `free` -- the values are small and
short-lived, and the runtime manages them.

---

## 1. Scalars

Six scalar helpers that come up constantly in graphics and DSP code:

```turmeric
(defmodule demo
  (import math/math :refer [clamp lerp remap deg->rad rad->deg approx-eq])

  (defn main [] : int
    (println (clamp 1.5 0.0 1.0))        ;; 1.0
    (println (lerp 0.0 10.0 0.5))         ;; 5.0
    (println (remap 5.0 0.0 10.0 0.0 1.0)) ;; 0.5
    (println (deg->rad 180.0))            ;; 3.14159...
    (println (rad->deg 3.14159265358979)) ;; ~180.0
    (println (approx-eq 0.1 0.10001 1e-4)) ;; true
    0))
```

```sweet-exp
defmodule demo
  import math/math :refer [clamp lerp remap deg->rad rad->deg approx-eq]

  defn main [] : int
    println $ clamp(1.5 0.0 1.0)         ;; 1.0
    println $ lerp(0.0 10.0 0.5)        ;; 5.0
    println $ remap(5.0 0.0 10.0 0.0 1.0) ;; 0.5
    println $ deg->rad(180.0)           ;; 3.14159...
    println $ rad->deg(3.14159265358979) ;; ~180.0
    println $ approx-eq(0.1 0.10001 1e-4) ;; true
    0
```

| Function | Formula |
|----------|---------|
| `clamp x lo hi` | `max(lo, min(x, hi))` |
| `lerp a b t` | `a + t*(b-a)` |
| `remap x in-lo in-hi out-lo out-hi` | `out-lo + ((x-in-lo)/(in-hi-in-lo)) * (out-hi-out-lo)` |
| `deg->rad deg` | `deg * pi/180` |
| `rad->deg rad` | `rad * 180/pi` |
| `approx-eq a b eps` | `|a-b| < eps` |

---

## 2. Vectors

`vec2`, `vec3`, and `vec4` share the same shape: construct with the
type-named function, access components with `vN-x` / `vN-y` / ..., and
operate with the `vN-*` family.

```turmeric
(defmodule demo
  (import math/vec3 :refer [vec3 v3-add v3-sub v3-scale v3-dot v3-cross
                           v3-length v3-normalize v3-lerp v3-x v3-y v3-z])

  (defn main [] : int
    (let [a (vec3 1.0 0.0 0.0)
          b (vec3 0.0 1.0 0.0)]
      (println (v3-x a))                 ;; 1.0
      (println (v3-cross a b))           ;; (0. 0, 0, 1.0)
      (println (v3-dot a b))             ;; 0.0
      (println (v3-length a))            ;; 1.0
      (println (v3-normalize (vec3 3.0 0.0 0.0)))  ;; (1.0, 0.0, 0.0)
      (println (v3-lerp a b 0.5))         ;; (0.5, 0.5, 0.0)
      0))
```

```sweet-exp
defmodule demo
  import math/vec3 :refer [vec3 v3-add v3-sub v3-scale v3-dot v3-cross
                           v3-length v3-normalize v3-lerp v3-x v3-y v3-z]

  defn main [] : int
    let [a vec3(1.0 0.0 0.0)
         b vec3(0.0 1.0 0.0)]
      println $ v3-x a                 ;; 1.0
      println $ v3-cross a b           ;; (0.0, 0.0, 1.0)
      println $ v3-dot a b             ;; 0.0
      println $ v3-length a            ;; 1.0
      println $ v3-normalize $ vec3(3.0 0.0 0.0)  ;; (1.0, 0.0, 0.0)
      println $ v3-lerp a b 0.5        ;; (0.5, 0.5, 0.0)
    0
```

`vec2` and `vec4` follow the same pattern with their respective component
counts. `vec4` is commonly used for RGBA colors.

| Operation | vec2 | vec3 | vec4 |
|-----------|------|------|------|
| construct | `vec2 x y` | `vec3 x y z` | `vec4 x y z w` |
| add | `v2-add` | `v3-add` | `v4-add` |
| sub | `v2-sub` | `v3-sub` | `v4-sub` |
| scale | `v2-scale v s` | `v3-scale v s` | `v4-scale v s` |
| dot | `v2-dot` | `v3-dot` | `v4-dot` |
| length | `v2-length` | `v3-length` | -- |
| normalize | `v2-normalize` | `v3-normalize` | -- |
| lerp | `v2-lerp a b t` | `v3-lerp a b t` | `v4-lerp a b t` |
| cross | -- | `v3-cross` | -- |

---

## 3. Matrices

`mat4` is a 4x4 column-major matrix. Build transforms with the constructor
functions, compose them with `mat4-mul`, and invert with `mat4-invert`:

```turmeric
(defmodule demo
  (import math/mat4 :refer [mat4-identity mat4-translate mat4-rotate-y
                           mat4-scale mat4-mul mat4-invert
                           mat4-perspective mat4-look-at])

  (defn main [] : int
    ;; Build a model matrix: translate then rotate
    (let [model (mat4-mul (mat4-translate 0.0 0.0 -5.0)
                          (mat4-rotate-y 0.785))]
      ;; Build a view matrix
      (let [view (mat4-look-at 0.0 0.0 3.0  0.0 0.0 0.0  0.0 1.0 0.0)]
        ;; Build a projection matrix
        (let [proj (mat4-perspective 0.785 (/ 800.0 600.0) 0.1 100.0)]
          ;; Compose: proj * view * model
          (let [mvp (mat4-mul proj (mat4-mul view model))]
            (mat4-invert mvp)            ;; invertible; returns identity if singular
            0)))))
```

```sweet-exp
defmodule demo
  import math/mat4 :refer [mat4-identity mat4-translate mat4-rotate-y
                           mat4-scale mat4-mul mat4-invert
                           mat4-perspective mat4-look-at]

  defn main [] : int
    ;; Build a model matrix: translate then rotate
    let [model mat4-mul(mat4-translate(0.0 0.0 -5.0)
                        mat4-rotate-y(0.785))]
      ;; Build a view matrix
      let [view mat4-look-at(0.0 0.0 3.0  0.0 0.0 0.0  0.0 1.0 0.0)]
        ;; Build a projection matrix
        let [proj mat4-perspective(0.785 /(800.0 600.0) 0.1 100.0)]
          ;; Compose: proj * view * model
          let [mvp mat4-mul(proj mat4-mul(view model))]
            mat4-invert(mvp)             ;; invertible; returns identity if singular
            0
```

| Function | Produces |
|----------|----------|
| `mat4-identity` | 4x4 identity |
| `mat4-translate tx ty tz` | translation matrix |
| `mat4-rotate-x/y/z angle` | rotation around the named axis (radians) |
| `mat4-scale sx sy sz` | scale matrix |
| `mat4-mul a b` | `a * b` (column-major) |
| `mat4-invert m` | inverse, or identity if singular |
| `mat4-perspective fovy aspect near far` | perspective projection |
| `mat4-look-at eye center up` | view matrix (each as a vec3 handle) |

`mat4-look-at` takes three vec3 handles (eye, center, up) rather than nine
floats -- build them with `vec3` first.

### Using mat4 with opengl

The `opengl/math` module provides its own float32-precision mat4 functions
that return pointers compatible with `set-uniform-mat4`. Use those when
uploading to shaders; use `tur-math`'s mat4 for double-precision computation.

---

## 4. Quaternions

Quaternions represent rotations without gimbal lock. Build one from an
axis-angle pair, compose with `q-mul`, interpolate with `q-slerp`, and
convert to a mat4 when you need to upload to a shader:

```turmeric
(defmodule demo
  (import math/vec3 :refer [vec3])
  (import math/quat :refer [quat q-from-axis-angle q-mul q-normalize
                           q-slerp q-to-mat4])

  (defn main [] : int
    (let [axis (vec3 0.0 1.0 0.0)
          q1   (q-from-axis-angle axis 0.0)
          q2   (q-from-axis-angle axis 1.5707963267948966)]  ;; 90 degrees
      ;; Halfway between the two orientations
      (let [mid (q-slerp q1 q2 0.5)]
        ;; Convert to a rotation matrix for upload
        (q-to-mat4 mid)
        0)))
```

```sweet-exp
defmodule demo
  import math/vec3 :refer [vec3]
  import math/quat :refer [quat q-from-axis-angle q-mul q-normalize
                           q-slerp q-to-mat4]

  defn main [] : int
    let [axis vec3(0.0 1.0 0.0)
         q1   q-from-axis-angle(axis 0.0)
         q2   q-from-axis-angle(axis 1.5707963267948966)]  ;; 90 degrees
      ;; Halfway between the two orientations
      let [mid q-slerp(q1 q2 0.5)]
        ;; Convert to a rotation matrix for upload
        q-to-mat4(mid)
        0
```

| Function | Produces |
|----------|----------|
| `quat x y z w` | quaternion from raw components |
| `q-from-axis-angle axis angle` | quaternion from a vec3 axis and radians |
| `q-mul a b` | Hamilton product (compose rotations) |
| `q-normalize q` | unit quaternion |
| `q-slerp a b t` | spherical linear interpolation |
| `q-to-mat4 q` | 4x4 rotation matrix |

`slerp` takes the shortest arc: if the dot product of the two quaternions
is negative, it flips one before interpolating, so the interpolation never
takes the long way around.

---

## When not to use this

- **You need a full linear-algebra library** (eigendecomposition, SVD,
  matrix factorizations). This is a small graphics-math library, not a
  numerical computing package.
- **You need SIMD or batch operations.** The operations are scalar inline-C;
  no vectorization.
- **You need double-precision mat4 for shader upload.** Use `opengl/math`
  instead -- its mat4 functions produce float32 data compatible with
  `set-uniform-mat4`.

---

## See also

- [API reference](api/)
- [README](../../spices/math/README.md)
- [tur-opengl guide](opengl-guide.html) -- the graphics pipeline this spice feeds
- [tur-raylib guide](raylib-guide.html) -- the higher-level graphics alternative
