# tur-c-dsl

Lisp-syntax DSL that compiles to C99 source code. Build C source trees from
Turmeric data structures and emit ready-to-compile `.c` strings.

## Overview

`tur-c-dsl` is a Tier 1 spice (pure Turmeric, no C deps). It models C
declarations, statements, and expressions as Turmeric values, then renders
them to a formatted C99 source string via `compile-c`.

It is most useful as a code-generation target -- e.g. emitting wrappers,
shader-side data layouts, or compiled DSLs -- where building C source by
string concatenation would be error-prone.

## Install

```turmeric no-check
:spices {
  "c-dsl" {:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "c-dsl-v0.2.0"
           :subdir "spices/c-dsl"}
}
```

## Quick start

```turmeric
(import c-dsl/codegen :refer [compile-c c-stmts])
(import c-dsl/fns     :refer [c-defn c-param c-call])
(import c-dsl/core    :refer [c-return c-binop])

(let [body (c-stmts (vec-of (c-return (c-binop "+" "a" "b"))))
      fn   (c-defn "add"
                   (list (c-param "a" ":int")
                         (c-param "b" ":int"))
                   ":int" body)]
  (println (compile-c (list fn))))
```

```sweet-exp
#lang sweet-exp
import c-dsl/codegen :refer [compile-c c-stmts]
import c-dsl/fns     :refer [c-defn c-param c-call]
import c-dsl/core    :refer [c-return c-binop]

let [body c-stmts(vec-of(c-return(c-binop("+" "a" "b"))))
     fn   c-defn("add"
                 list(c-param("a" ":int") c-param("b" ":int"))
                 ":int" body)]
  println $ compile-c list(fn)
```

## Fix-encoded IR (`c-dsl/ir`)

Alongside the flat string-template builders, `c-dsl/ir` offers a recursive IR
encoded as `Fix CNodeF` with a single catamorphism. Build a `CNode` tree with
the `ce-*` (expression) and `cs-*` (statement) smart constructors, then fold it
with `node->c` to render C source -- byte-identical to the equivalent builder
calls -- or `node-size` to count nodes. Every fold goes through one generic
`node-cata` driver, so adding a new traversal is just a new F-algebra with no
per-node scaffolding.

```turmeric
(import c-dsl/ir :refer [ce-var ce-lit ce-binop cs-return node->c])

;; (x + 1) folded to C, then wrapped in a return statement
(node->c (cs-return (ce-binop "+" (ce-var "x") (ce-lit "1"))))
;; => "return (x + 1);"
```

```sweet-exp
(import c-dsl/ir :refer [ce-var ce-lit ce-binop cs-return node->c])

;; (x + 1) folded to C, then wrapped in a return statement
node->c $ cs-return $ ce-binop "+" ce-var("x") ce-lit("1")
;; => "return (x + 1);"
```

The flat builders remain the stable public surface; the IR is an additive,
self-contained recursion-schemes layer. See the module header in
`src/c-dsl/ir.tur` for the design rationale (notably why it uses one functor
rather than two).

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/c-dsl>
