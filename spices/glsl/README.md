# tur-glsl

Lisp-syntax DSL that compiles to GLSL shader source. Author vertex and
fragment shaders in Turmeric and emit GLSL strings ready to feed to
`tur-opengl` (or any other GL backend).

## Overview

`tur-glsl` is a Tier 1 spice (pure Turmeric, no C deps). It mirrors the
shape of `tur-c-dsl` but targets GLSL: shader stages, uniforms, inputs,
outputs, and the standard GLSL built-ins are modeled as Turmeric values
and rendered by `compile-glsl`.

Pair it with `tur-opengl` for an end-to-end "shader in Turmeric" workflow,
or use it stand-alone to bake GLSL strings at build time.

## Install

```turmeric no-check
:spices {
  "glsl" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "glsl-v0.2.0"
          :subdir "spices/glsl"}
}
```

## Quick start

```turmeric
(import glsl/shaders  :refer [glsl-fragment-shader glsl-output])
(import glsl/core     :refer [glsl-set!])
(import glsl/codegen  :refer [compile-glsl])

(println
  (compile-glsl
    (glsl-fragment-shader "330 core"
      (vec-of (glsl-output "FragColor" ":vec4"))
      (glsl-set! "FragColor" "vec4(1.0, 0.5, 0.2, 1.0)"))))
```

```sweet-exp
#lang sweet-exp
import glsl/shaders :refer [glsl-fragment-shader glsl-output]
import glsl/core    :refer [glsl-set!]
import glsl/codegen :refer [compile-glsl]

println
  compile-glsl
    glsl-fragment-shader "330 core"
      vec-of(glsl-output("FragColor" ":vec4"))
      glsl-set!("FragColor" "vec4(1.0, 0.5, 0.2, 1.0)")
```

## Fix-encoded IR (`glsl/ir`)

Alongside the flat string-template builders, `glsl/ir` offers a recursive IR
encoded as `Fix GNodeF` with a single catamorphism. Build a `GNode` tree with
the `ge-*` (expression, including the glsl-specific `ge-swizzle`) and `gs-*`
(statement) smart constructors, then fold it with `node->c` to render GLSL
source -- byte-identical to the equivalent builder calls -- or `node-size` to
count nodes. Every fold goes through one generic `node-cata` driver, so adding a
new traversal is just a new F-algebra with no per-node scaffolding.

```turmeric
(import glsl/ir :refer [ge-var ge-binop ge-swizzle gs-return node->c])

;; return (a + b).xyz;
(node->c (gs-return (ge-swizzle (ge-binop "+" (ge-var "a") (ge-var "b")) ":xyz")))
;; => "return (a + b).xyz;"
```

```sweet-exp
(import glsl/ir :refer [ge-var ge-binop ge-swizzle gs-return node->c])

;; return (a + b).xyz;
node->c $ gs-return $ ge-swizzle ge-binop("+" ge-var("a") ge-var("b")) ":xyz"
;; => "return (a + b).xyz;"
```

The flat builders remain the stable public surface; the IR is an additive,
self-contained recursion-schemes layer. See the module header in
`src/glsl/ir.tur` for the design rationale (notably why it uses one functor
rather than two, carried over from the c-dsl prototype's memo).

> **0.3.0 note:** the `length`, `dot`, `min`, and `max` builtins were renamed
> to `glsl-length`, `glsl-dot`, `glsl-min`, and `glsl-max` to avoid collisions
> with auto-loaded stdlib names. The emitted GLSL is unchanged.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/glsl>
