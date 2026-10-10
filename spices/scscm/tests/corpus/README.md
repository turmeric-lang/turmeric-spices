# scscm compile-parity corpus

The 21 `.scscm` programs hypercollider ships (`test/fixtures/scscm_parity/`,
`tests/fixtures/scscm/`, `cli/examples/`), copied verbatim at the commit in
`HYPERCOLLIDER_COMMIT`. `SOURCES` lists each one's origin and whether its
sclang is expected to parse.

| File | What it is |
|---|---|
| `<name>.scscm` | the program, as hypercollider has it |
| `<name>.expected.scd` | what tur-scscm emits; `../corpus_test.tur` compares byte for byte |
| `<name>.lhc.scd` | what hypercollider's `cli/lhc.js` emits, kept for reference |
| `regen.sh` | rebuilds all three from a hypercollider checkout |
| `check-sclang.sh` | parses every `.expected.scd` with a real `sclang` (`--lhc`: the references) |

## Why the golden files are not lhc.js's output

The plan this corpus comes from (turmeric
`docs/upcoming/spices/scscm-host-testing-plan.md`, T1) meant to use `lhc.js`
as the golden. Measured with stock SuperCollider 3.13's parser
(`check-sclang.sh --lhc`), 19 of its 21 outputs do not parse; tur-scscm's
parse for all 19 programs whose input is valid scscm. So the language is
hypercollider's, and the golden is tur-scscm's output once a real sclang
accepts it and it says what the program says. The reference outputs stay
here so a change in `lhc.js` shows up as a diff.

What stops each `lhc.js` output (first error; most have several):

| Program | lhc.js output | Why |
|---|---|---|
| `parity_defn` | fails | `defn` assigns an undeclared `double` |
| `parity_hello`, `example_hello_scheme` | fail | `var` after a statement |
| `parity_if`, `parity_var` | parse | |
| `fixture_basics` | fails | `+5` is not a literal (the input also uses undefined names) |
| `fixture_control_flow` | fails | `when` emits `{ ... }()`, which is not a call in sclang |
| `fixture_functions` | fails | a zero-parameter `defn` emits `{ \|\| 3.14159 }`; `get-pi()` keeps the hyphen in call position |
| `fixture_quoting` | fails | `'foo` opens a quoted symbol that runs to the next `'`; `'(1 2 3)` becomes `'1(2, 3)` |
| `fixture_synths` | fails | input is Overtone syntax (`out:ar`), not scscm |
| `example_ambient_step_1` to `_4`, `example_quick_start_1` and `_2`, `example_sine_wave`, `example_fm_synthesis` | fail | `(* a b c)` becomes `*(a, b, c)` |
| `example_overtone_fm`, `example_overtone_intro`, `example_overtone_sequencer` | fail | `definst` emits `{ ... }()`; `__args` is not an identifier (a leading `_` is primitive syntax); callers use `sine-wave(...)` |

Further `lhc.js` outputs parse but do the wrong thing:

- `(dict :doneAction 2)` becomes the Event `(doneAction: 2)`, passed
  positionally, so `EnvGen.kr(env, gate, (doneAction: 2))` sets `levelScale`
  rather than `doneAction`.
- `(Synth "x" (dict :freq 440))` becomes `Synth("x", (freq: 440))`;
  `Event.asOSCArgArray` is nil in SuperCollider, so the synth gets none of
  its controls.
- `pbind` becomes `Pbind.new([...])`, which throws "Pbind should have even
  number of args".
- `defsynth` omits the `.add` its own documentation shows.
- `(* a (+ b c))` becomes `a * b + c`, which sclang (no precedence) reads as
  `(a * b) + c`.
- A rest parameter is emitted `|a, ...rest|`, which does not parse.

## Programs that are not valid scscm

`SOURCES` marks two programs `invalid-input`; `check-sclang.sh` requires that
their output does not parse, so a change that starts accepting them is
noticed: `fixture_basics` (references `foo`, `bar` and `some-symbol`, which
nothing defines) and `fixture_synths` (Overtone's `out:ar` syntax).

Three parse but make no sound, which is the program's doing, not the
compiler's: `example_sine_wave` and `example_fm_synthesis` build a SynthDef
without `.add`, and `example_overtone_sequencer` builds a Pbind it never
plays. `../host/PROGRAMS` leaves them out.

## Regenerating

```sh
tests/corpus/regen.sh <hypercollider-checkout>   # needs node and tur
git diff tests/corpus                            # review every change
tests/corpus/check-sclang.sh                     # and that it still parses
```

`regen.sh` warns when the checkout is not at `HYPERCOLLIDER_COMMIT`; bump the
file when you mean to move the corpus.
