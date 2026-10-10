---
title: scscm to SuperCollider
category: Audio
description: Compile scscm, a Scheme-like syntax for SuperCollider, to sclang with tur-scscm, then run it on scsynth or hypercollider's hcsynth -- live or rendered to a WAV
audience: developers writing SuperCollider programs in s-expressions, and anyone testing tur-scscm against a real synth server
since: scscm v0.1.0
---

# tur-scscm Guide

scscm is SuperCollider in s-expressions: `(SinOsc.ar 440)` for
`SinOsc.ar(440)`, `(fn (freq 440) ...)` for `{ |freq=440| ... }`. `tur-scscm`
compiles it to sclang, SuperCollider's language. That is all it does: neither
`scsynth` nor hypercollider's `hcsynth` reads sclang, so the output always
runs through an sclang interpreter, which builds the SynthDefs and sends the
server its OSC.

```
scscm --[tur-scscm]--> sclang --[sclang | hclang]--> OSC, SynthDefs --> scsynth | hcsynth --> audio
```

1. [Installing the spice](#1-installing-the-spice)
2. [Compiling](#2-compiling)
3. [The language](#3-the-language)
4. [Running on scsynth](#4-running-on-scsynth)
5. [Running on hcsynth](#5-running-on-hcsynth)
6. [Testing](#6-testing)
7. [Troubleshooting](#7-troubleshooting)

Every command below was run on Ubuntu 24.04 with tur v0.63.9, SuperCollider
3.13.0 and hypercollider built from source; where a step was *not* run, the
text says so.

---

## 1. Installing the spice

In your project's `build.tur`:

```turmeric
:spices #{
  "scscm" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
            :ref    "main"
            :subdir "spices/scscm"}
}
```

Then `tur fetch`. Pure Turmeric plus inline C; no system libraries.

## 2. Compiling

`scscm/compile` has two functions. Both return the lexer's ok/err box, not a
`result`:

```turmeric no-check
(compile-text source :cstr) :ptr<void>   ;; lex-ok(sclang) or lex-err(message)
(compile-file path :cstr)   :ptr<void>
```

Test it with `lex-ok?`; the value is a `:cstr` cast to `:int`, read back with
`scscm-int-as-cstr`. Both need `#fx{Unsafe}` in the caller, and printing adds
`IO`:

```turmeric no-check
(defmodule demo
  (import scscm/compile :refer [compile-text])
  (import scscm/lexer   :refer [lex-ok? lex-ok-val lex-err-val scscm-int-as-cstr])

  (defn main [] #fx{Unsafe IO} : int
    (let [r (compile-text "(defn double (x) (* x 2)) (double 21)")]
      (if (lex-ok? r)
        (do (println (scscm-int-as-cstr (lex-ok-val r))) 0)
        (do (println (scscm-int-as-cstr (lex-err-val r))) 1)))))
```

prints

```supercollider
var double = { |x| x * 2 };
double.(21);
```

An error is a message with a position: `unclosed '(' opened at line 2 col 3`,
`unexpected ')' at line 1 col 6`, `unterminated string literal at line 1 col
4`, `cannot read x.scscm: No such file or directory`.

The spice ships that as a command, `examples/scscmc.tur`. From the spice
directory (note the `--`: `tur run` passes nothing to the program without it):

```sh
tur run examples/scscmc.tur -- examples/beep.scscm > beep.scd
```

`examples/beep.scscm` is the program the rest of this guide runs:

```scheme
(defsynth beep (freq 440 amp 0.2)
  (Out.ar 0 (* (SinOsc.ar freq 0) (perc 0.01 0.99) amp)))

(defn play-beep (freq)
  (Synth "beep" (dict :freq freq)))

(play-beep 440)
(in 0.5 (play-beep 660))
```

and `beep.scd` is

```supercollider
var play_beep;
SynthDef("beep", { |freq=440, amp=0.2| Out.ar(0, SinOsc.ar(freq, 0) * EnvGen.kr(Env.perc(0.01, 0.99), doneAction: 2) * amp) }).add;
play_beep = { |freq| Synth("beep", [\freq, freq]) };
play_beep.(440);
TempoClock.default.sched(0.5, { play_beep.(660) });
```

## 3. The language

The language is hypercollider's scscm. Its reference is
[`docs/scscm/SCSCM_LANGUAGE_REFERENCE.md`](https://github.com/rjungemann/hypercollider/blob/main/docs/scscm/SCSCM_LANGUAGE_REFERENCE.md)
in hypercollider, and its compiler is `cli/lhc.js`. The spice's
[README](../../spices/scscm/README.md#the-language-in-one-table) has the
one-table summary. Two habits from other Lisps carry over: parameter and
binding lists may be written `(a b)` or `[a b]`, and comments are `;`.

The spice does **not** reproduce `lhc.js`'s text. Most of `lhc.js`'s output
does not parse in SuperCollider: run through stock sclang 3.13, 19 of
hypercollider's 21 example programs fail, against none of the 19 that are
valid scscm when compiled by the spice
([`tests/corpus/README.md`](../../spices/scscm/tests/corpus/README.md) has
the per-program list). Where the two differ, the spice follows what the
reference *documents* the form means and emits sclang that says it:

| Form | lhc.js | tur-scscm | Why |
|---|---|---|---|
| `(defn f (x) ...)`, `(var x 1)` | `f = {...}`, `var x = 1` wherever it falls | `var` declarations first; a later one is hoisted to a leading `var x;` | sclang accepts `var` only at the start of a block, and `f` is undeclared |
| `(f 1)`, f a variable | `f(1)` | `f.(1)` | `f(1)` is the message `1.f` |
| `(* a b c)` | `*(a, b, c)` | `a * b * c` | not sclang |
| `(* a (+ b c))` | `a * b + c` | `a * (b + c)` | sclang has no precedence: left to right |
| `(do a b)` | `{ a; b }()` | `a; b`, or `(a; b)` in an expression | `{ }()` is not a call |
| `(fn (a . rest) ...)` | `{ \|a, ...rest\| }` | `{ \|a ...rest\| }` | the comma does not parse |
| `(dict :doneAction 2)` as a last argument | `(doneAction: 2)`, positionally | `doneAction: 2`, a keyword argument | positionally it is `EnvGen`'s `levelScale` |
| `(Synth "x" (dict :freq 440))` | `Synth("x", (freq: 440))` | `Synth("x", [\freq, 440])` | an Event gives `Synth` no controls |
| `(pbind :dur 0.25)` | `Pbind.new([\dur, 0.25])` | `Pbind(\dur, 0.25)` | the former throws at runtime |
| `(defsynth n ...)` | `SynthDef(...)` | `SynthDef(...).add` | as the reference documents it |
| `'foo`, `'(1 2)` | `'foo`, `'1(2)` | `\foo`, `#[1, 2]` | |
| `+5`, `(- x)` | `+5`, `-(x)` | `5`, `x.neg` | |
| generated names | `__args` | `scscm_args` | a leading `_` is primitive syntax |

Names map as in the reference: `foo-bar` -> `foo_bar`, `done?` -> `done_q`,
`set!` -> `set`, `:cut-off` -> `\cut_off`. A `(dict ...)` that is not a
call's last argument is an Event, `(freq: 440)`.

`defmacro` is a template, not hypercollider's macro evaluator: parameters are
replaced by the argument forms, then `` ` `` / `~` / `~@` apply. Macros are
per program.

## 4. Running on scsynth

### Installing SuperCollider

- **Ubuntu 24.04** (verified): `sudo apt-get install supercollider-server
  supercollider-language` gives 3.13.0.
- **macOS**: the `supercollider` Homebrew cask. *Not verified here.*

`sclang` without a display needs `QT_QPA_PLATFORM=offscreen`, and as root
also `QTWEBENGINE_DISABLE_SANDBOX=1` (QtWebEngine refuses to start
otherwise):

```sh
printf '"hi".postln;\n0.exit;\n' > hi.scd
QT_QPA_PLATFORM=offscreen sclang hi.scd     # ... prints hi
```

### Live (verified with JACK's dummy driver)

Boot a server, then run the compiled program:

```supercollider
// play.scd
s.waitForBoot { thisProcess.interpreter.executeFile("beep.scd".resolveRelative) };
```

```sh
sclang play.scd
```

Run this way, `beep.scd`'s first note fails: `*** ERROR: SynthDef beep not
found`. `SynthDef(...).add` is asynchronous, so the `Synth` sent right
behind it reaches the server before the definition has loaded. (The offline
renders below never show this, because they apply every message in order.)
Wait for the server between adding and playing. In scscm that is `(. s
sync)` inside a `(routine ...)`; `s` is sclang's default server. That is
`examples/beep-live.scscm`:

```scheme
(routine
  (. s sync)          ; wait until the server has beep
  (play-beep 440)
  (wait 0.5)
  (play-beep 660))
```

which compiles to

```supercollider
Routine({ s.sync; play_beep.(440); 0.5.wait; play_beep.(660) }).play;
```

and plays both notes live. The machine these were run on has no audio
device: `jackd -d dummy -r 48000` stood in for one, and `s.record` captured
what the server played.

### Offline, to a WAV (verified)

`tests/host/capture.scd` runs a program in sclang against a stand-in for the
server's address: `SynthDef.add`, `Synth`, `Pbind.play` and
TempoClock-scheduled code all run as they would live, for as many seconds of
real time as you ask, and every message is recorded with its time. It writes
those as a score for `scsynth -N`:

```sh
QT_QPA_PLATFORM=offscreen sclang spices/scscm/tests/host/capture.scd beep.scd beep.osc 2.0
scsynth -N beep.osc _ beep.wav 48000 WAV int16 -o 2
python3 spices/scscm/tests/host/wavcheck.py beep.wav \
  pitch=440@0.05-0.45 pitch=660@1.05-1.45 silent@1.6-1.9
```

```
captured 3 messages
ok   pitch=440@0.05-0.45: pitch 440.0 Hz, want 440
ok   pitch=660@1.05-1.45: pitch 660.0 Hz, want 660
ok   silent@1.6-1.9: rms 0.0000, want < 0.001
```

The score uses the SynthDefs *this* sclang writes, so use the sclang that
matches the scsynth.

## 5. Running on hcsynth

hypercollider is a SuperCollider fork (3.15-dev) compiled to WebAssembly:
`hclang` is its sclang, `hcsynth` its server.

### Getting hclang (built from source)

No hypercollider release boots today: v0.1.5 and v0.1.6 stop with `class
library compilation FAILED`, and their `--classlib-dir` does not get around
it (the embedded pack still shadows the files). The fixes are on the
`claude/scscm-host-testing-plan-8s8b5c` branch of rjungemann/hypercollider
(hypercollider PRs [#1](https://github.com/rjungemann/hypercollider/pull/1)
and [#2](https://github.com/rjungemann/hypercollider/pull/2); the commit is
in `spices/scscm/tests/host/HOSTS`). Built with emsdk 5.0.5, the version
hypercollider's native CI uses:

```sh
git clone https://github.com/emscripten-core/emsdk.git && cd emsdk
./emsdk install 5.0.5 && ./emsdk activate 5.0.5 && source ./emsdk_env.sh
cd ../hypercollider
(cd cli && npm install)          # the build runs hclang once, under node
cmake --preset wasm_release
cmake --build build/wasm --target hclang hcsynth -j"$(nproc)"
```

The same branch also builds `hclang_native`, a single native binary that
runs the WASI build of hclang under WAMR, with no Node:

```sh
cmake --preset wasi_release
cmake --build build/wasi --target hclang hcsynth -j"$(nproc)"
cmake -B build/native -S native -DCMAKE_BUILD_TYPE=Release -DHC_EMBED_WASM=ON
cmake --build build/native --target hclang_native -j"$(nproc)"
build/native/hclang_host/hclang_native --script hello.scd
```

The host tests below use the Node `hclang`, whose `--output` capture
`hcsynth` renders.

Run `hclang` from the checkout's root (it finds `build/wasm/` from the
working directory), and pass `--verbose` to see what the program posts:

```sh
node cli/hclang.js --verbose --no-zeroconf --script hello.scd --output hello.json
```

### Offline, to a WAV (verified)

`hclang --output` writes the OSC the program sent while it was evaluated;
`hcsynth --commands` renders it:

```sh
node cli/hclang.js --no-zeroconf --script beep.scd --output beep.json
node cli/hcsynth.js --commands beep.json --output beep.wav --duration 2 --block-size 64
```

```
✓ Wrote 2 OSC packets to beep.json
[OK] Rendered 2 packets to beep.wav
```

Two packets: the SynthDef and the first beep. **The capture has no clock**
(`beep-live.scscm` captures only its SynthDef, since everything it plays is
inside a routine):
`hclang` exits after evaluating the script, so nothing scheduled -- the
second beep, a `Pbind`'s events, `(in ...)`, `(at ...)` -- is ever sent. Use
it for programs that make their sound immediately; render the rest on
scsynth.

Pass `--block-size 64`. hcsynth's default block is 512 samples, which also
sets its control rate: `EnvGen.kr` then steps at 94 Hz instead of scsynth's
750 Hz, and the first 100 ms of an envelope differ from scsynth's by 7%.

### Forwarding to a server

`hclang --scsynth-host 127.0.0.1 --scsynth-port 57110` sends the OSC to a
running server instead. Before hypercollider #2 it sent nothing: hclang
closed its socket while the packets were still queued. With #2 they arrive.
Against stock SuperCollider 3.13 that still does not play anything (verified
with a live scsynth): hypercollider's sclang writes version-3 SynthDefs
(`SynthDef.writeDef` defaults to `version=3`), and scsynth 3.13 accepts the
`/d_recv` but does not load the definition, so every `/s_new` fails with
`SynthDef not found`, even a second later. hcsynth's own server mode (`node
cli/hcsynth.js --server`) is the matching server; *not verified*.

### scscm files directly

`hclang --script prog.scscm` (or `--lang scscm`) compiles the program with
hypercollider's own `lhc.js`, not with this spice, so it inherits the
differences in [section 3](#3-the-language). `--scscm-debug` writes what
`lhc.js` produced to `prog.scscm.compiled.sc`. To run the spice's output,
compile first and pass the `.scd`.

## 6. Testing

Three layers, cheapest first, all in `spices/scscm/tests/`:

| Layer | Command | Needs | Checks |
|---|---|---|---|
| Compile | `tur test tests` | tur | `compile_test.tur` (50 cases), and `corpus_test.tur`: hypercollider's 21 programs against `corpus/*.expected.scd`, byte for byte |
| sclang | `tests/corpus/check-sclang.sh` | `sclang` | every expected file parses (two invalid inputs must not) |
| Audio | `tests/host/render.sh` | `sclang` + `scsynth`, and/or `HC_DIR` | renders `host/PROGRAMS` and checks pitch, level and silence; on both hosts, the WAVs must agree |

`render.sh` prints `SKIP: no scsynth` / `SKIP: no hcsynth` for a missing host
rather than failing. With both:

```sh
HC_DIR=/path/to/hypercollider spices/scscm/tests/host/render.sh
```

```
ok example_quick_start_1 scsynth: pitch=440@0.2-0.9: pitch 440.0 Hz, want 440
ok example_quick_start_1 scsynth: rms=0.0566@0.2-0.9: rms 0.0566, want 0.0566
ok example_quick_start_1 hcsynth: pitch=440@0.2-0.9: pitch 440.0 Hz, want 440
...
ok example_quick_start_1 scsynth~hcsynth: agree over 0-0.9s
...
all checks passed
```

The tolerances, from measurement: a pitch within 2% (zero crossings located
to a fraction of a sample measure a held sine to 0.1 Hz), a level within 15%
of its expected value, and two renders agree when every 0.1 s window's RMS is
within 5% and its pitch within 2%. Rendered at the same block size, scsynth
3.13 and hcsynth agree to within 0.0006 RMS per 0.1 s window on the two
programs both can render.

CI (`.github/workflows/scscm-hosts.yml`) runs the sclang and scsynth layers on
Ubuntu with the SuperCollider version pinned in `tests/host/HOSTS`. The
hcsynth layer runs by hand until a hypercollider release boots.

## 7. Troubleshooting

**hclang: `class library compilation FAILED`, or `hc_wasm_eval_boot_sequence
failed (-1)`.** The release's class library does not compile; build from the
branch in section 5. The errors come from `HC/Scscm*.sc`,
`HCNetAddrZeroconf.sc`, `Quarks.sc` and `String.sc`.

**hclang_native: `filesystem_error was thrown in -fno-exceptions mode`, or
`out of bounds memory access` after `compile done`.** Both are fixed on the
branch: the first was the WASI directory walk, the second a host import that
shifted its arguments (it failed for some environments and not others, e.g.
under `env -i`).

**hclang: `Wrote 0 OSC packets`, and `Primitive '_NetAddr_SendMsg' failed`
with `--verbose`.** The CLI installed its OSC bridge under a name the WASM side
does not read. Fixed on the same branch.

**hclang prints nothing.** It shows the program's posts only with
`--verbose`.

**hclang: `Module JS not found: .../build/wasm/lang/hclang/hclang.js`.** Run it
from the hypercollider checkout's root.

**hcsynth: the sound does not stop when the synth frees.** Older hcsynth kept
repeating the last 512-sample block; fixed on the branch. With the fix, a
freed synth is silence.

**sclang: `Variable 'foo' not defined`.** The program uses a name nothing
defines; sclang has no free variables. (`a` to `z` are predefined.)

**scsynth: `SynthDef not found`.** Either the program never adds the
SynthDef (`(SynthDef "x" ...)` builds one; `(. (SynthDef "x" ...) add)` or
`defsynth` sends it), or it plays one before the asynchronous add has
finished -- live only; wait with `(. s sync)` in a routine (section 4) --
or the SynthDef is version 3 (from hclang) on a 3.13 scsynth.

**No sound from a `pbind`.** A Pbind is a description; `(. (pbind ...) play)`
plays it.

**sclang will not start headless.** `QT_QPA_PLATFORM=offscreen`, and as root
`QTWEBENGINE_DISABLE_SANDBOX=1`.
