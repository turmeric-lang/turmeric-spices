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

`tur fetch` builds Manifold v3.5.5, the Clipper2 commit it pins and oneTBB
v2022.3.0 from source and links all three statically. That needs a C++17
compiler, CMake 3.20 or newer, git, and network access at fetch time. A cold
build takes about 3 minutes 45 seconds and leaves roughly 535 MB under
`cmake/build/`. `tur fetch` compiles one file at a time unless told otherwise;
`CMAKE_BUILD_PARALLEL_LEVEL=4 tur fetch` brings it to about 65 seconds on four
cores.

It needs a `tur` newer than v0.73.0. That release and earlier ones do not put
the C++ runtime or oneTBB on the link line, so the first program fails to link
on undefined symbols, `operator new` among them.

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

A loop can accumulate into a `^mut` solid. `union` consumes the old value and
`set!` puts the result in its place, so the binding holds exactly one solid
before and after every iteration:

```turmeric
(let [^mut acc (empty-solid)
      ^mut x   0.0]
  (while (< x 9.0)
    (set! acc (union acc (translate (cube 1.5 1.5 1.5 false) x 0.0 0.0)))
    (set! x (+ x 2.5)))
  acc)
```

```sweet-exp
let [^mut acc empty-solid()
     ^mut x   0.0]
  while {x < 9.0}
    set!(acc union(acc translate(cube(1.5 1.5 1.5 false) x 0.0 0.0)))
    set!(x {x + 2.5})
  acc
```

The new value has to use up the old one in its own expression. Assigning over
a solid that is still live -- `(set! acc (cube 1.5 1.5 1.5 false))` -- would
orphan it, and is rejected.

A `SolidVec` holds a list of solids instead. `solid-vec-push!` borrows the
vector and consumes the solid, and one batch operation consumes the vector:

```turmeric
(let [parts  (solid-vec-new)
      ^mut x 0.0]
  (while (< x 9.0)
    (solid-vec-push! parts (translate (cube 1.5 1.5 1.5 false) x 0.0 0.0))
    (set! x (+ x 2.5)))
  (hull-all parts))
```

```sweet-exp
let [parts  solid-vec-new()
     ^mut x 0.0]
  while {x < 9.0}
    solid-vec-push!(parts translate(cube(1.5 1.5 1.5 false) x 0.0 0.0))
    set!(x {x + 2.5})
  hull-all(parts)
```

`union-all`, `intersection-all` and `hull-all` consume the vector;
`solid-vec-free` releases one you decide not to use. `decompose` returns one.

### What the compiler catches

Each of these is a compile error, pinned by a fixture under `errors/`:

| Mistake | Diagnostic |
|---|---|
| A bound solid or vector that is never consumed | `TUR-E0100` |
| Using a solid after `solid-free`, or after an operation took it | `TUR-E0101` |
| Passing one handle as both operands, `(union a a)` | `TUR-E0101` |
| Consuming a solid in one branch of an `if` but not the other | `TUR-E0104` |
| Freeing a solid inside a function that only borrowed it | `TUR-E0109` |
| An operation written as a statement, its result bound to nothing | `TUR-E0100` |
| `set!` over a solid that has not been consumed | `TUR-E0100` |

### What the checker does not catch

A solid that never gets a name is followed only part of the way, so these two
shapes compile and leak. They were measured with turmeric main as of
2026-11-27, under LeakSanitizer, and turmeric keeps them open as
`docs/reported/linear-values-without-a-binding-are-not-checked.md`.

| Shape | Why it leaks | Write instead |
|---|---|---|
| `(volume (cube 1.5 1.5 1.5 true))` | a temporary handed to a borrowing function has no owner | bind it, or use `with-solid` |
| a `Solid` stored in a struct field or a vector literal | the aggregate is not linear | a `SolidVec` |

The spice's own tests run under LeakSanitizer in CI (`TUR_SANITIZE_PROGRAMS=1`),
which does catch both at run time. Doing the same in your project is the
practical guard.

## Threading

Manifold is built with its parallel backend (`MANIFOLD_PAR=ON`). A call still
returns only when its work is done, but a large boolean, hull or query splits
that work across oneTBB's worker threads -- by default as many as the machine
has hardware threads. Nothing in this API starts or configures those threads;
they belong to oneTBB.

What this does not make safe is your own threads. The spice has only been
tested with each `Solid` used from the thread that made it, and says nothing
about handing one to another Turmeric thread.

oneTBB is compiled in as a static archive (`MANIFOLD_USE_BUILTIN_TBB=ON`), so
nothing has to be installed first. Manifold warns that a static oneTBB can
misbehave in a program that also links another copy of it, which matters only
if a second native dependency of yours brings its own.

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
| [oneTBB](https://github.com/oneapi-src/oneTBB) | v2022.3.0, the one Manifold pins | Apache-2.0 (no `NOTICE` file) |

Manifold's tree also carries code under other terms, each stated in the file's
own header:

| File in Manifold | Origin | Terms |
|---|---|---|
| `src/svd.h` | wi-re, 2019 | MIT |
| `src/disjoint_sets.h` | Wenzel Jakob's `dset`, 2015 | zlib-style |
| `include/manifold/math.h` | FreeBSD msun, via musl | Sun Microsystems permission notice |
| `include/manifold/linalg.h` | Sterling Orsten's `linalg.h` | public domain |
| `src/quickhull.cpp`, `src/quickhull.h` | Antti Kuukka's QuickHull | public domain |

oneTBB's `third-party-programs.txt` lists six components. One is compiled into
the archive this spice links: the ITT Notify API, listed there under a
BSD-3-Clause notice. The others are not part of it: hwloc, gperftools, a
Python recipe and doctest are not linked, and the libstdc++ workaround is
compiled only against libstdc++ 4.7 to 5.

All of these are permissive licenses. If you distribute a binary built with
this spice, Apache-2.0 asks you to pass on a copy of that license, and the MIT,
BSD and Sun notices ask to be preserved. This is a summary of what the files say,
not legal advice.

## See also

- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/manifold>
- Upstream Manifold: <https://github.com/elalish/manifold>
