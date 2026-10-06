# tur-watch

Cross-platform filesystem watcher (inotify on Linux, kqueue on Darwin)
with debounce and coalescing helpers. Designed for CLI tools that need a
"file changed, react" loop without each tool re-inventing a watch loop.

> **Status:** v0.2.0 -- per-file naming in tree mode (WTNF1-WTNF6).
> `watch-open-tree` events now carry the file path, not just the directory.
> Darwin recovers names via directory-snapshot diff; Linux uses inotify names.

## Modules

| Module | What's in it |
|---|---|
| `watch/event` | `watch-event` record, `watch-kind-*` constants, accessors |
| `watch/opts`  | `watch-opts` record, `default-watch-opts` (150 ms debounce) |
| `watch/watch` | `watch-open-one`, `watch-open-tree`, `watch-close`, `watch-next`, `watch-drain` |
| `watch/debounce` | `debounce-batch-*` primitive for path coalescing |

`watch/backend` is internal (the inotify+kqueue inline-C bridge) and is
not exported. Public callers should never import it.

## Quick start

```turmeric
(import watch/event :refer [watch-event-kind watch-event-path watch-event-free
                             watch-kind-write watch-kind-rename])
(import watch/opts  :refer [default-watch-opts])
(import watch/watch :refer [watch-open-one watch-close watch-next])

(defn main [] :int
  (let [opts (default-watch-opts)
        w    (watch-open-one "notes.md" opts)]
    (do
      (loop []
        (let [ev (watch-next w -1)]   ; block forever
          (if (= ev 0)
            0
            (do
              (println "file changed -- rerender now")
              (watch-event-free ev)
              (recur)))))
      (watch-close w))))
```

For recursive directory watching use `watch-open-tree`:

```turmeric
(import watch/opts  :refer [watch-opts-make watch-opts-free])
(import watch/watch :refer [watch-open-tree watch-close watch-next])
(import watch/event :refer [watch-event-path watch-event-kind
                             watch-kind->cstr watch-event-free])

(defn main [] :int
  (let [opts (watch-opts-make 1 150 1 0 1)   ; recursive, 150 ms debounce
        w    (watch-open-tree "src/" opts)]
    (do
      (watch-opts-free opts)
      (loop []
        (let [ev (watch-next w -1)]
          (if (= ev 0)
            0
            (do
              ;; watch-event-path is now the FILE path, e.g. "src/foo/bar.tur"
              (println (watch-kind->cstr (watch-event-kind ev)))
              (println (watch-event-path ev))
              (watch-event-free ev)
              (recur)))))
      (watch-close w))))
```

## Event kinds

| Kind | Numeric | When it fires |
|---|---|---|
| `watch-kind-write`    | 1 | file contents changed |
| `watch-kind-create`   | 2 | file appeared |
| `watch-kind-delete`   | 3 | file disappeared |
| `watch-kind-rename`   | 4 | inode changed -- typical for atomic-save editors |
| `watch-kind-attrib`   | 5 | metadata-only change |
| `watch-kind-overflow` | 6 | backend queue overflowed; fall back to full rebuild |

## Capability matrix

| Capability | v0.1.0 | v0.2.0 |
|---|---|---|
| Single-file watch | yes | yes (unchanged) |
| Atomic-save (write temp + rename) on Linux + Darwin | yes | yes (unchanged) |
| In-place writes on Linux | yes | yes (unchanged) |
| In-place writes on Darwin | **no event** (see note) | **no event** (see note) |
| Recursive directory watch (`watch-open-tree`) | yes (dir-level only) | yes, **per-file naming** |
| Per-file path in tree-mode events | **no** (dir path only) | **yes** (both Linux + Darwin) |
| Per-file kind classification in tree mode | **no** (always `write`) | **yes** |
| Darwin: per-file names via snapshot diff | **no** | **yes** |
| Linux: per-file names via inotify name field | **no** | **yes** |
| Pending-event queue (multi-event drains) | **no** | **yes** |
| `debounce-batch-coalesce` deduplication in tree mode | no (all same path) | **yes** |
| `watch-add-path` / `watch-remove-path` | stubs return -1 | stubs return -1 |
| Callback / async API | no | no |
| macOS FSEvents backend | no | no (planned v0.3) |

> **Note on in-place writes on Darwin.** Both backends register the parent
> *directory*, and `kqueue`'s `EVFILT_VNODE` on a directory fires for
> entry-level changes (create, unlink, rename) but not for in-place content
> modification of a child file -- so a plain `fopen("w")` or `O_APPEND` write
> over an existing target produces no Darwin event at all, and the
> snapshot/inode diff never gets a chance to classify it. This table
> previously claimed a `kind=write via size diff` here; that was wrong.
> Atomic-save flows (write temp + rename), which is what vim, emacs and
> VS Code do by default, fire on both backends. Measured on macOS 27,
> 2026-09-09; see `docs/notebook-watch-semantics.md` section 10.
| Windows backend | no | no |

### v0.2.0 semantics change for tree-mode callers

`watch-event-path` in tree mode previously returned the directory that fired.
It now returns the relative file path (e.g. `"src/foo/bar.tur"` instead of
`"src/foo"`). Callers that only need "something changed, trigger full rebuild"
can ignore the path -- the event still arrives, the path is just more precise.

## Architecture (v0.2.0)

```
  client (tail-events / build-watch / notebook)
                |
                v
          watch/watch              <-- pending queue + produce-events dispatch
                |
        +-------+--------+
        |                |
        v                v
   watch/debounce    watch/event
        |
        v
   watch/backend                   <-- platform inline-C + evbuf (WTNF1)
        |                           -- backend-drain-into fills TurBackendEvent
        v
  inotify  or  kqueue
        |
        v
  watch/watch: __watcher-tree-produce-events
     Linux:  iterate evbuf, classify inotify mask
     Darwin: snapshot diff (TurDirSnapshot per dir, taken at open + each drain)
```

## Examples

| Example | What it does |
|---|---|
| `examples/tail-events.tur`    | Print every event as it arrives (single file or recursive dir) |
| `examples/rerun-command.tur`  | Run a shell command on each debounced change |
| `examples/watch-tree.tur`     | Recursive directory watcher with a registered-dirs roster |

Run any of them with `tur run examples/<name>.tur -- <path>`.

## Testing

```sh
tur run tests/event_test.tur
tur run tests/opts_test.tur
tur run tests/backend_smoke_test.tur
tur run tests/backend_drain_into_test.tur   # WTNF1 -- evbuf + names
tur run tests/debounce_test.tur
tur run tests/watch_test.tur                # spawns background pthreads
tur run tests/drain_burst_test.tur          # single-file burst collapse
tur run tests/tree_test.tur                 # WT6 + WTNF2/3 per-file naming
tur run tests/tree_burst_test.tur           # WTNF4 multi-file coalesce
```

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/watch-guide.html)
- [tur-watch guide](https://turmeric-lang.com/docs/html/guides/tur-watch-guide.html)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/watch>
