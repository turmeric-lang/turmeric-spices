# turmeric-spices -- Claude Code Guide

This repo holds the spice packages that build against the `tur` compiler from
the sibling repo `turmeric-lang/turmeric`. The compiler is **not** built from
source here -- fetch a prebuilt binary instead.

## Reading the sibling `turmeric` repo -- STRICT RULE

Reading source, docs, fixtures, plans, or history from `turmeric-lang/turmeric`
is **always allowed**, even when it is not checked out locally. The
single-repo sandbox is a working directory, not an enforcement boundary.
"We only have spices checked out" is **never** a valid reason to refuse to
look something up in turmeric -- a stdlib signature, a plan under
`docs/upcoming/`, an archived report under `docs/archive/`, a fixture, a
commit message, or anything else the user references by path or name.

Use whichever fetch path is convenient:

- `gh api repos/turmeric-lang/turmeric/contents/<path>` (single file, base64).
- `gh api repos/turmeric-lang/turmeric/git/trees/main?recursive=1` (full tree listing).
- `git clone --depth=1 https://github.com/turmeric-lang/turmeric /tmp/turmeric`
  then grep/read locally. `/tmp/turmeric` is a read-only scratch copy; do not
  treat it as a second working tree, do not commit to it, do not push from it.
- `WebFetch` on a `https://raw.githubusercontent.com/turmeric-lang/turmeric/main/<path>` URL.

If a user mentions a turmeric concept (e.g. "yyjson plan", "tur-signal gate",
a doc filename) and the file is not in this repo, **fetch it before claiming
ignorance or blockage**. Quoting a passage from the turmeric repo back to the
user is a normal read operation, not a cross-repo violation.

What you **cannot** do from a turmeric-spices-rooted session: open PRs,
push branches, or land commits against `turmeric-lang/turmeric`. That constrains
writes only; it does not constrain reads. If the work the user wants requires
*writing* to turmeric, stop and tell them to re-launch a session rooted there
(or do it from local Claude Code); do not silently degrade to "I can't help."

## Getting the `tur` binary (do this first on any new sandbox)

Run the install script. It downloads the matching prebuilt release tarball
from `turmeric-lang/turmeric`, verifies the SHA-256, extracts it under
`vendor/tur/`, and prints the export line you need:

```sh
./scripts/install-tur.sh
eval "$(./scripts/install-tur.sh)"   # or: export TUR_BIN="$PWD/vendor/tur/tur"
```

- Pin a specific version: `TUR_VERSION=v0.13.0 ./scripts/install-tur.sh`
- Force a redownload: `./scripts/install-tur.sh --force`
- Supported platforms: `macos-arm64`, `linux-x86_64`, `linux-aarch64`
- The release pipeline lives at `turmeric-lang/turmeric/.github/workflows/release.yml`
  and runs on every `v*` tag push. If `releases/latest` returns nothing, the
  most recent tag predates the pipeline -- pin `TUR_VERSION` to a tag that has
  binary assets attached, or push a new tag from the turmeric repo.

**Do not build `tur` from source as a default.** Source builds are the
fallback when no release asset exists for your platform, not the happy path.
Each cold sandbox doing a CMake build wastes minutes per session.

## Repo layout

```
spices/        -- one directory per spice package; each has its own build.tur
docs/          -- spice guides (rendered to HTML) and generated API docs
build.tur      -- top-level manifest
vendor/tur/    -- prebuilt tur binary + stdlib (gitignored; created by install-tur.sh)
scripts/       -- developer tooling (install-tur.sh, ...)
```

The tarball that `install-tur.sh` extracts contains `tur`, `libturi.a`,
`include/turi/*.h`, and the full `stdlib/` tree. The stdlib lives next to
the binary on purpose -- do not move them apart.

## Cross-spice development (workspace-local imports)

All spices in this repo are listed as `:members` of the root `build.tur`.
That makes them a workspace, so one sibling can import another **without
`tur fetch`, a symlink, or a lockfile entry**.

```sh
cd spices/notebook
tur check src/notebook/cli.tur   # resolves watch/watch via workspace member
```

The first undeclared sibling import prints a one-time advisory:

```
warning: import 'watch/watch' resolved via workspace sibling 'spices/watch';
         declare it in :spices for release builds.
```

To declare a local dep explicitly (for editor/LSP autocomplete or to avoid the
advisory), use a `:path` entry in `build.tur`:

```turmeric
:spices {
  "watch" {:path "../watch"}
}
```

`tur fetch --dry-run` shows which deps would be fetched vs skipped locally.
Local-source deps never produce a `tur.lock` entry. Do not hand-edit
`tur.lock` to add stub entries for workspace siblings -- that workaround is
no longer needed.

### `tur.lock` is generated, and is NOT committed

Every `spices/*/tur.lock` is gitignored, despite the "Commit this file to
version control for reproducible builds" header tur writes into it. That
header describes an intent the toolchain does not implement: nothing ever
checks out a locked SHA. `tur fetch` without `--update` treats the presence
of a lock *row* as "this dep is already on disk" and skips the fetch
outright, so on a cold clone a committed lock leaves the dependency silently
missing -- strictly worse than having no lock. The clone itself goes through
`pkg_git_fetch(url, ref, dest)`, by `:ref` and never by `:resolved`, and the
`:fetched-at` wall-clock stamp means every fetch rewrites the file anyway.

So: never `git add` a `tur.lock`, and do not "restore" one you see missing.
`.gitignore` carries the full reasoning. Revisit if tur learns to check out
`:resolved`.

The same goes for `spices/*/cmake/` -- `CMakeLists.txt` and
`spice-deps-manifest.json` are both stamped "AUTO-GENERATED by `tur fetch`.
Do not edit", and `tur new` scaffolds ignores for them into every new spice.
Committing them is worse than churn: `tur test` / `tur build` read the
manifest *if present* and pass its `include_dirs` / `link_dirs` straight to
the compiler, while CMake only rewrites it on a successful configure -- so a
failed fetch leaves a committed manifest in place and the build silently uses
whatever paths it holds. Several used to hold another machine's
`/Users/...` paths.

## CI

`.github/workflows/ci.yml` currently checks out `turmeric-lang/turmeric` and
builds `tur` from source on every run. That is intentional for CI's
reproducibility guarantees (CI verifies spices against tip-of-main turmeric,
not against a release). For **local** and **agent sandbox** work, prefer the
prebuilt path above.

## Writing tests that can actually fail

Two traps, both of which produce a green suite that checks nothing. Both are
silent -- no diagnostic, no warning.

**A `describe` block must sit inside a `defn`.** A `(describe ...)` written as
a direct child of `defmodule` is dropped by the compiler: it is typechecked
and then discarded, so `tur emit-c` shows a `main` that calls
`run-all-status` and nothing else. The suite prints `1..0 / All 0 tests
passed` and exits 0. Wrap the blocks in a `(defn __all-tests [] : void ...)`
and call it from `main` before `run-all-status`.

**`main` must return `run-all-status`, not a hardcoded `0`.** `tur test`
judges a file by its exit code; `(run-all)` returns void, so a failing `it`
prints `not ok` and the suite still reports success.

Related shapes worth knowing, each found the hard way:

- A `defn` nested inside another `defn` is always a missing close paren. The
  outer body then ends on a definition rather than a value and the emitted C
  returns 0 unconditionally.
- An inline ```c block in **statement** position (not the last form of its
  defn) whose body contains a top-level `return` returns from the whole
  enclosing function. Everything after it is dead code. Put the `return` in a
  defn of its own.
- A `: nil` self-recursive `defn` makes codegen bind a void call to
  `__auto_type`, which cc rejects ("variable or field declared void").
  Return `int` with an explicit base case.
- A module-level `(def x (...))` whose initialiser has a `:linear` type (e.g.
  stdlib's `Mutex`) emits no global at all; the C then fails on an undeclared
  identifier. Hold the carrier instead: `(def m (:: (mutex-new) :int))`, and
  cast back at each borrow.

## `:cmake-deps` that the fetch heuristics cannot describe

Without `:targets`, `tur fetch` guesses a dep's include dir
(`${SOURCE_DIR}/include`, else `${SOURCE_DIR}`) and its `-l` name (the dep or
target basename). Several real libraries defeat both guesses: a
header-only library with its header in `src/`; a target whose `OUTPUT_NAME`
differs from its name (glfw builds `libglfw3.a`); a find_package import whose
target is `PostgreSQL::PostgreSQL` while the library is `libpq`; a "library"
that is really a code generator (glad).

The fix is a small CMake project under `spices/<spice>/cmake-deps/<name>/`
that produces a target whose name **is** the `-l` name, declared as a `:path`
cmake-dep with `:targets`:

```turmeric
:cmake-deps #map{
  "raygui" #map{:path    "../cmake-deps/raygui"
                :targets ["raygui"]}}
```

With `:targets`, tur reads include dirs from the target's
`INTERFACE_INCLUDE_DIRECTORIES` and link dirs from `$<TARGET_FILE_DIR:...>`
instead of guessing. Note the leading `../`: `:path` is spliced in as
`add_subdirectory("./<path>")` inside `cmake/`, so the path is relative to
that directory, not to the spice root. See `raygui`, `opengl` and `postgres`
for worked examples.

A missing header does **not** fail the build: hoisted includes are emitted as
`#if __has_include(<x.h>) ... #endif`, so a wrong include dir degrades
silently into implicit declarations and confusing type errors much later.
