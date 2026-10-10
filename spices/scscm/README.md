# tur-scscm

An scscm (Scheme-like s-expression syntax for SuperCollider) to sclang
compiler written in Turmeric. scscm text goes in, sclang text comes out; run
that with SuperCollider's `sclang` or hypercollider's `hclang`.

```
scscm --[tur-scscm]--> sclang --[sclang | hclang]--> OSC, SynthDefs --> scsynth | hcsynth --> audio
```

The language is hypercollider's scscm (its reference is
[`docs/scscm/SCSCM_LANGUAGE_REFERENCE.md`](https://github.com/rjungemann/hypercollider/blob/main/docs/scscm/SCSCM_LANGUAGE_REFERENCE.md),
its compiler `cli/lhc.js`). The spice emits sclang that a stock SuperCollider
3.13 `sclang` parses, which in many places `lhc.js` does not; the
[guide](../../docs/guides/scscm-guide.md#3-the-language) lists where the two
differ and why. There is no OSC client: the spice does not talk to a server.

Start with the [guide](../../docs/guides/scscm-guide.md): it covers
installing, the compile API, running the output on scsynth and on hcsynth,
and the host tests.

## Overview

`tur-scscm` is a Tier 1 spice (pure Turmeric + inline-C, no cmake deps). The
pipeline:

```
scscm text
  -> [lexer]    token list            (scscm/lexer)
  -> [parser]   AST                   (scscm/parser)
  -> [expander] core forms            (scscm/expander: built-in and user macros)
  -> [codegen]  sclang text           (scscm/codegen)
```

`scscm/compile` is the one-shot API.

## Quick start

```turmeric no-check
(defmodule demo
  (import scscm/compile :refer [compile-text])
  (import scscm/lexer   :refer [lex-ok? lex-ok-val lex-err-val scscm-int-as-cstr])

  (defn main [] #fx{Unsafe IO} : int
    (let [r (compile-text "(defn double (x) (* x 2)) (double 21)")]
      (if (lex-ok? r)
        (do (println (scscm-int-as-cstr (lex-ok-val r))) 0)
        (do (println (scscm-int-as-cstr (lex-err-val r))) 1)))))
;; prints:
;;   var double = { |x| x * 2 };
;;   double.(21);
```

Or from the shell, with the bundled example driver:

```sh
tur run examples/scscmc.tur -- examples/beep.scscm > beep.scd
```

## The language in one table

| scscm | sclang |
|-------|--------|
| `nil` `true` `false` `440` `0.5` `"hi"` | the same |
| `:freq`, `:cut-off` | `\freq`, `\cut_off` |
| `foo-bar`, `done?`, `set!` | `foo_bar`, `done_q`, `set` |
| `(fn (a b) body)` | `{ \|a, b\| body }` |
| `(fn (freq 440 amp 0.1) body)` | `{ \|freq=440, amp=0.1\| body }` |
| `(fn (a . rest) body)` | `{ \|a ...rest\| body }` |
| `(defn f (a) body)` then `(f 1)` | `var f = { \|a\| body };` then `f.(1)` |
| `(var x 1)`, `(def x 1)` | `var x = 1` (hoisted when it follows a statement) |
| `(set! x 2)` | `x = 2` |
| `(let ((x 1) (y 2)) body)` | `{ \|x, y\| body }.value(1, 2)` |
| `(if c a b)` | `if (c) { a } { b }` |
| `(do a b)` | `a; b` in a body, `(a; b)` in an expression |
| `(. obj msg a)` / `(.dot Cls msg a)` | `obj.msg(a)` / `Cls.msg(a)` |
| `(SinOsc.ar 440)` | `SinOsc.ar(440)` |
| `(list 1 2)`, `[1 2]` | `[1, 2]` |
| `(dict :freq 440)` | `(freq: 440)`; as a call's last argument `freq: 440`; for `Synth`/`.set` `[\freq, 440]` |
| `'foo`, `'(1 2)` | `\foo`, `#[1, 2]` |
| `(* a (+ b c) d)` | `a * (b + c) * d` |

Built-in macros (hypercollider's): `->` `->>` `when` `unless` `cond` `doseq`
`dotimes` `collect` `icollect` `accumulate` `comp` `partial` `defsynth`
`definst` `routine` `wait` `pbind` `pseq` `prand` `pwhite` `pgeom` `ctl`
`kill` `now` `metronome` `at` `in` `demo` `midi->hz` `hz->midi` `db->amp`
`amp->db` `adsr` `perc` `env-line` `chord` `degrees->pitches`, and `#(+ % 1)`
for a short function. User macros:

```scscm
(defmacro my-inc (x)
  `(+ ~x 1))

(my-inc 5)   ; => 5 + 1
```

Macros are templates: each parameter is replaced by its argument form, then
`` ` `` / `~` / `~@` are applied. A `(. rest)` parameter collects the
remaining arguments for `~@rest`. Macros are per program: each
`compile-text` starts with none.

## API

All handles are `:int` (C pointers to heap structs), as the spice has always
done; results are the lexer's ok/err box.

### scscm/compile

```turmeric no-check
(compile-text source :cstr) :ptr<void>   ;; lex-ok(sclang) or lex-err(message)
(compile-file path :cstr)   :ptr<void>   ;; reads path, then compile-text
```

Both are `#fx{Unsafe}`. Test the box with `lex-ok?` / `lex-err?` and read it
with `lex-ok-val` / `lex-err-val` (from `scscm/lexer`); either value is a
`:cstr` cast to `:int`, read with `scscm-int-as-cstr`. Error messages name
the line and column, e.g. `unclosed '(' opened at line 2 col 3` or
`cannot read x.scscm: No such file or directory`.

### scscm/lexer

```turmeric no-check
(tokenize source :cstr) :ptr<void>   ;; lex-ok(token cons list) or lex-err(message)
(token-type tok :int) :cstr          ;; ":symbol" ":keyword" ":number" ...
(token-value tok :int) :cstr
(token-line tok :int) :int
(token-col tok :int) :int
(tokens-free ts :int) :void
```

### scscm/parser

```turmeric no-check
(parse tokens :int) :ptr<void>       ;; lex-ok(cons list of AST nodes) or lex-err(message)
(ast-kind node :int) :cstr           ;; ":list" ":symbol" ...
(ast-symbol-name node :int) :cstr
(ast-number-value node :int) :cstr
(ast-string-value node :int) :cstr
(ast-list-len node :int) :int
(ast-list-get node :int i :int) :int
(ast-free node :int) :void
(ast-free-all nodes :int) :void
```

### scscm/expander

```turmeric no-check
(expand ast :int) :int               ;; expand one node
(expand-all asts :int) :int          ;; expand a cons list
(scscm-macros-reset) :int            ;; forget defmacros (compile-text does this)
```

### scscm/codegen

```turmeric no-check
(generate ast :int) :cstr            ;; sclang for one expression
(generate-all asts :int) :cstr       ;; a program: one statement per line, each ending ";"
```

## Tests

- `tur test tests` -- `compile_test.tur` (one rule per case, errors
  included), `corpus_test.tur` (21 programs from hypercollider against
  `tests/corpus/*.expected.scd`), `tree_test.tur` (`scscm/tree`).
- `tests/corpus/check-sclang.sh` -- every expected file parses in a real
  `sclang` (SKIP without one).
- `tests/host/render.sh` -- renders the sound-making programs on scsynth
  and/or hcsynth and checks pitch and level (SKIP without a host).

See [`tests/corpus/README.md`](tests/corpus/README.md) and the guide's
[testing section](../../docs/guides/scscm-guide.md#6-testing).

## Implementation notes

- The lexer (`scscm/lexer`) scans in one inline-C block; parsing, expansion
  and code generation are Turmeric over C-allocated AST nodes.
- The expander's macro table is a C static; `compile-text` resets it before
  and after each program, since it points into that program's AST.
- Codegen keeps the names in scope (variables, parameters, `let` bindings)
  on a C-static stack, to tell a call of a variable (`f.(x)`) from a message
  (`f(x)`, which sclang reads as `x.f`).

## Roadmap

- [x] SC0 -- lexer
- [x] SC1 -- parser
- [x] SC2 -- codegen
- [x] SC3 -- expander (special forms + quasiquote + user defmacro)
- [x] SC4 -- stdlib macros + compile API
- [x] SC5 -- tests: unit, the hypercollider corpus, sclang parse checks,
      rendered-audio checks on scsynth and hcsynth
- [ ] ~~SC6 -- `scscm/server`: OSC client for scsynth/hcsynth~~ -- out of
      scope: a server needs SynthDefs, which only sclang or hclang can build,
      so the output runs through one of them (see the guide)
