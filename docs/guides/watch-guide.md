# tur-watch -- Filesystem Watcher Guide

> Spice version 0.1.0 -- cross-platform filesystem watcher (Linux inotify,
> Darwin kqueue) with debounce + coalescing for CLI tools.
> Audience: Turmeric users writing a tool that needs to "react when a file
> changes" without re-inventing a watch loop per tool.

This guide walks the five things you'll do most often:

1. [Watch a single file](#1-watching-a-single-file)
2. [Recursive directory watch](#2-recursive-directory-watch)
3. [Drained batches + debounce](#3-drained-batches--debounce)
4. [Picking the right kind constant](#4-event-kinds)
5. [Adopting in your own CLI](#5-adopting-in-your-own-cli)

Each section is a self-contained snippet you can drop into a `defn main`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "watch" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "main"
            :subdir "spices/watch"}
}
```

Then run `tur fetch`. The spice has no external C dependencies; the
inotify / kqueue bridge lives inside `watch/backend` as inline-C.

---

## 1. Watching a single file

The simplest pattern: open, block, classify, repeat. `watch-next` returns
an event handle (or `0` on timeout) and you free the handle when done.

```turmeric
(defmodule app/main
  (import watch/event :refer [watch-event-kind watch-event-free
                              watch-kind->cstr])
  (import watch/opts  :refer [default-watch-opts])
  (import watch/watch :refer [watch-open-one watch-close watch-next])

  (defn main [] #{Unsafe} :int
    (let [w (watch-open-one "notes.md" (default-watch-opts))]
      (do
        (rerun-on-change w)
        (watch-close w)
        0)))

  (defn rerun-on-change [w :int] #{Unsafe} :int
    (let [ev (watch-next w -1)]   ; -1 = block forever
      (if (= ev 0)
        0
        (do
          (println (watch-kind->cstr (watch-event-kind ev)))
          (watch-event-free ev)
          (rerun-on-change w))))))
```

Pass a `default-watch-opts` handle (150 ms debounce window, coalesce on,
non-recursive) or build your own with `watch-opts-make`. Pass `0` for
`opts` and the watcher uses the same defaults internally.

**Atomic-save is the happy path.** Most editors save by writing a sibling
temp file and renaming it over the target. Both backends fire on the
rename and the watcher's stat-compare reports `watch-kind-rename`. In-place
writes also fire on Linux; on Darwin the kqueue-on-directory backend does
not see them (this is documented in `docs/upcoming/spices/notebook-watch-semantics.md`
in the turmeric repo).

---

## 2. Recursive directory watch

Pass a directory to `watch-open-tree` (or to `watch-open` with
`opts.recursive = 1`) and it walks the tree at startup, opening one
backend per subdir. Hidden entries (`.git`, `.tur-cache`, ...) and
symlinks are skipped to avoid cycles.

```turmeric
(let [opts (watch-opts-make 1 150 1 0 1)
      w    (watch-open-tree "src" opts)]
  (do
    (println-int (watch-tree-dir-count w))  ; how many dirs registered
    (loop-forever w)
    (watch-close w)
    (watch-opts-free opts)))
```

`watch-next` on a tree watcher reports the **directory** that fired, not
the changed file -- the kqueue backend on Darwin doesn't expose per-file
names. Callers that need a filename should re-enumerate the dir and diff.

If a new subdirectory appears inside the tree after open, call
`watch-refresh` to register it. v0.1.0 does not auto-refresh on
`watch-kind-create` events.

See `examples/watch-tree.tur` for the full demo.

---

## 3. Drained batches + debounce

Bursty save flows (vim writing several backup files, or a build tool
touching many files in one transaction) produce many backend events for
one logical change. The default 150 ms debounce window absorbs the burst
into a single emitted event.

For multi-path callers, use `watch-drain`:

```turmeric
(let [batch (watch-drain w 5000 150)]   ; 5 s timeout, 150 ms window
  (process-batch batch))                 ; cons-list of event handles
```

v0.1.0 single-file watch always produces a 0- or 1-element batch
(`watch-next` already handles burst-collapse internally). Tree mode is
the same shape today; multi-element batches will arrive when path-level
classification lands.

If you need to dedupe paths inside a batch yourself, `watch/debounce`
exposes the primitive:

```turmeric
(let [b (debounce-batch-make)]
  (debounce-batch-push b ev1)
  (debounce-batch-push b ev2)
  (debounce-batch-coalesce b)   ; same-path runs collapse to the latest
  (println-int (debounce-batch-len b)))
```

---

## 4. Event kinds

```
watch-kind-write    = 1   file contents changed (Linux only; see §1)
watch-kind-create   = 2   target appeared
watch-kind-delete   = 3   target disappeared
watch-kind-rename   = 4   inode changed -- typical for atomic-save
watch-kind-attrib   = 5   metadata-only change
watch-kind-overflow = 6   backend queue overflowed; fall back to full rebuild
```

Always handle `watch-kind-overflow` explicitly. The backend's event queue
is bounded; under heavy load the OS drops events, and we surface that as
an explicit kind rather than silently missing changes. The right response
is usually "treat as 'everything changed' and re-scan from scratch."

Numeric values are part of the public ABI -- safe to read `.kind`
directly from inline-C if you really need to.

---

## 5. Adopting in your own CLI

`tur-notebook` is the reference adopter -- `tur nb render --watch`
delegates to `watch-open-one + watch-next + watch-close`. The full
diff is small: open one watcher, loop on `watch-next`, free the event,
re-run the work, repeat. See
`spices/notebook/src/notebook/cli.tur` (search for `__cli-watch-loop`)
for the actual pattern.

The contract `tur-watch` v0.1.0 promises any adopter:

- Atomic-save (write temp + rename) on Linux and Darwin produces exactly
  one debounced event per save.
- Sibling-file activity in the watched directory does not wake the
  caller (the stat-compare layer filters it).
- `watch-close` is safe to call from any state, including immediately
  after a failed open.
- Backend queue overflow becomes an explicit `watch-kind-overflow`
  event, not a silent drop.

What v0.1.0 does **not** yet provide:

- Multi-path explicit lists in `watch-open` -- single path only today.
- Per-file naming in recursive-tree events on Darwin.
- macOS `FSEvents` backend, Windows backend, callback / async API.
- Ignore globs / path filters.

These land in v0.2 once a third real adopter (likely `tur repl --watch`)
forces the design.

---

---

## Appendix: `tur repl --watch` and tur-watch

The spice plan flags `tur repl --watch` as a candidate adopter (WT9). The
current REPL `--watch` lives in the C codebase at
`turmeric/src/turi/repl.c` and uses a polling `mtime` check
(`tur_spice_image_is_fresh`) right before each prompt evaluation. That
implementation is correct and simple, but the user only sees the reload
*after* they hit Enter -- it can't interrupt a long readline wait when
a file changes mid-edit.

A tur-watch-backed REPL `--watch` would:

1. open a `watch-open-tree` on the spice's `src/` at startup,
2. run a background pthread blocked on `watch-next`,
3. on each event, mark the spice image dirty and (optionally) post a
   pseudo-keystroke to break readline out of its wait so the reload runs
   immediately.

That is a meaningful UX win but a non-trivial refactor inside the
turmeric C codebase, not a spice-side change. It is intentionally
**deferred from tur-watch v0.1.0**. Notebook is the proven adopter for
this release; the REPL can adopt later once someone is willing to do the
async interrupt work in `repl.c`.

If you are building a Turmeric-side REPL or task runner that wants
hot-reload-on-change behavior today, the loop in `examples/rerun-command.tur`
is the recommended pattern.

## See also

- [tur-watch front page](https://spices.turmeric-lang.com/watch/) -- module map and module-level docs
- [docs/upcoming/spices/notebook-watch-semantics.md](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/spices/notebook-watch-semantics.md) -- the contract tur-watch v0.1.0
  preserves from the original notebook watcher
