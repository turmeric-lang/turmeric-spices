# tur-manifold

Solid modeling for Turmeric: constructive solid geometry on meshes that are
guaranteed to stay manifold (watertight, no self-intersections), wrapping
[Manifold](https://github.com/elalish/manifold) through its C API.

> **Status: prototype (0.1.0).** This is the first slice -- primitives,
> booleans, transforms, hulls, queries and batch operations -- built to settle
> how solids are owned. Cross-sections, mesh import/export, level sets and a
> raylib viewer are not here yet. Verified on Linux x86-64; the macOS build is
> untested until CI runs it.

## Install

```turmeric no-check
:spices {
  "manifold" {:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "manifold-v0.1.0"
              :subdir "spices/manifold"}
}
```

`tur fetch` builds Manifold v3.5.5 and its Clipper2 dependency from source and
links them statically. That needs a C++17 compiler, CMake 3.20 or newer, git,
and network access at fetch time. A cold build takes about 45 seconds on four
cores and leaves roughly 220 MB under `cmake/build/`.

## Quick start

A plate with a hole through it:

```turmeric
(import manifold/solid :refer [cube cylinder difference volume genus solid-free])

(let [plate (difference (cube 4.5 2.5 0.5 true)
                        (cylinder 1.5 0.75 0.75 48 true))
      v (volume plate)
      g (genus plate)]
  (solid-free plate)
  (println v)
  (println g))
```

```sweet-exp
#lang sweet-exp
import manifold/solid :refer [cube cylinder difference volume genus solid-free]

let [plate difference(cube(4.5 2.5 0.5 true)
                      cylinder(1.5 0.75 0.75 48 true))
     v volume(plate)
     g genus(plate)]
  solid-free(plate)
  println(v)
  println(g)
```

This prints `4.74395` and `1`: the plate's 5.625 less the 48-sided cylinder
drilled out of it, and one handle.

## Ownership

Manifold solids are immutable -- every operation makes a new one -- and each
one is a C++ object that has to be deleted. `Solid` is a `^linear` handle, so
the compiler checks that every solid you bind is consumed exactly once.

| Kind | Functions | What happens to a `Solid` argument |
|---|---|---|
| Constructors | `cube` `cylinder` `sphere` `tetrahedron` `empty-solid` | none taken; returns a fresh one |
| Operations | `union` `difference` `intersection` `translate` `rotate` `scale` `mirror` `hull` `decompose` | **consumed**; returns a fresh one |
| Queries | `volume` `surface-area` `genus` `num-vert` `num-edge` `num-tri` `empty?` `solid-status` `solid-ok?` | borrowed |
| Copy | `solid-copy` | borrowed; returns a second handle |
| Release | `solid-free` | consumed |

Operations consume their operands so that an expression can nest: in
`(difference (cube ...) (cylinder ...))` neither operand has a name, and
`difference` is what frees them. Only the outermost result needs a binding and
a `solid-free`.

Since an operation uses up its operand, give it a copy when the solid is
needed again. `solid-copy` shares the geometry, so it costs a reference, not a
mesh:

```turmeric
(let [a (cube 1.5 1.5 1.5 false)
      both (union (solid-copy a) (translate (solid-copy a) 2.5 0.0 0.0))]
  (solid-free a)
  both)
```

```sweet-exp
let [a cube(1.5 1.5 1.5 false)
     both union(solid-copy(a) translate(solid-copy(a) 2.5 0.0 0.0))]
  solid-free(a)
  both
```

`with-solid` binds a solid for a body and frees it afterwards:

```turmeric
(with-solid [d (difference (cube 2.5 2.5 2.5 true) (sphere 1.5 48))]
  (volume d))
```

```sweet-exp
with-solid [d difference(cube(2.5 2.5 2.5 true) sphere(1.5 48))]
  volume(d)
```

### Loops

A `while` loop cannot accumulate by rebinding: `(set! acc (union acc part))` is
rejected with `TUR-E0101`, because assigning `acc` again does not make it
usable again once `union` has consumed it. Push into a `SolidVec` instead --
`solid-vec-push!` borrows the vector and consumes the solid -- and let one
batch operation consume the vector:

```turmeric
(let [parts (solid-vec-new)
      ^mut x 0.0]
  (while (< x 9.0)
    (solid-vec-push! parts (translate (cube 1.5 1.5 1.5 false) x 0.0 0.0))
    (set! x (+ x 2.5)))
  (union-all parts))
```

```sweet-exp
let [parts solid-vec-new()
     ^mut x 0.0]
  while {x < 9.0}
    solid-vec-push!(parts translate(cube(1.5 1.5 1.5 false) x 0.0 0.0))
    set!(x {x + 2.5})
  union-all(parts)
```

`union-all`, `intersection-all` and `hull-all` consume the vector;
`solid-vec-free` releases one you decide not to use. A recursive function
whose accumulator is a parameter needs none of this.

### What the compiler catches

Each of these is a compile error, pinned by a fixture under `errors/`:

| Mistake | Diagnostic |
|---|---|
| A bound solid or vector that is never consumed | `TUR-E0100` |
| Using a solid after `solid-free`, or after an operation took it | `TUR-E0101` |
| Passing one handle as both operands, `(union a a)` | `TUR-E0101` |
| Consuming a solid in one branch of an `if` but not the other | `TUR-E0104` |
| Freeing a solid inside a function that only borrowed it | `TUR-E0109` |

### What the checker does not catch

The linearity check follows named bindings. A solid that never gets a name is
invisible to it, so these three shapes compile and leak. They were measured
with turmeric main as of 2026-11-24, under LeakSanitizer, and are reported as
turmeric `docs/reported/linear-values-without-a-binding-are-not-checked.md`.

| Shape | Why it leaks | Write instead |
|---|---|---|
| `(volume (cube 1.5 1.5 1.5 true))` | a temporary handed to a borrowing function has no owner | bind it, or use `with-solid` |
| `(translate s 2.5 0.0 0.0)` as a statement | the result is never bound or consumed | bind the result |
| a `Solid` stored in a struct field or a vector literal | the aggregate is not linear | a `SolidVec` |

The spice's own tests run under LeakSanitizer in CI (`TUR_SANITIZE_PROGRAMS=1`),
which does catch all three at run time. Doing the same in your project is the
practical guard.

## Threading

Manifold is built **single-threaded** here (`MANIFOLD_PAR=OFF`). Every
operation runs on the thread that calls it, and a large boolean uses one core
however many the machine has. Manifold's parallel backend needs oneTBB, which
`tur fetch` cannot currently put on the link line: Manifold links it through a
generator expression, and tur's link walk drops those (turmeric
`docs/reported/cmake-dep-genex-link-entry-dropped.md`).

Manifold also evaluates lazily. A boolean returns immediately with a node of a
CSG tree; the mesh is computed the first time something asks about it. The
cost of a long chain of operations therefore shows up in the first query --
`volume`, `num-tri`, `solid-status` -- not in the operations themselves.

## Licensing

The Turmeric source in this spice is MIT, like the other bindings in this
repository. Nothing from Manifold is vendored here: `tur fetch` downloads it
at build time. A program built with this spice, however, statically links the
following, checked against the sources at the pinned versions:

| Component | Version | License |
|---|---|---|
| [Manifold](https://github.com/elalish/manifold) | v3.5.5 | Apache-2.0 (no `NOTICE` file) |
| [Clipper2](https://github.com/AngusJohnson/Clipper2) | commit `46f6391`, the one Manifold pins | BSL-1.0 |

Manifold's tree also carries code under other terms, each stated in the file's
own header:

| File in Manifold | Origin | Terms |
|---|---|---|
| `src/svd.h` | wi-re, 2019 | MIT |
| `src/disjoint_sets.h` | Wenzel Jakob's `dset`, 2015 | zlib-style |
| `include/manifold/math.h` | FreeBSD msun, via musl | Sun Microsystems permission notice |
| `include/manifold/linalg.h` | Sterling Orsten's `linalg.h` | public domain |
| `src/quickhull.cpp`, `src/quickhull.h` | Antti Kuukka's QuickHull | public domain |

All of these are permissive licenses. If you distribute a binary built with
this spice, Apache-2.0 asks you to pass on a copy of that license, and the MIT
and Sun notices ask to be preserved. This is a summary of what the files say,
not legal advice.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/manifold>
- Upstream Manifold: <https://github.com/elalish/manifold>
