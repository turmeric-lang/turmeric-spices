---
title: Signal Processing with Typed Signal Functions
category: Audio
description: Oscillators, filters, shapers, ADSR envelopes, and SF-pipeline composition -- a typed Signal/SF abstraction for building audio DSP graphs
audience: developers building audio synthesis, DSP pipelines, or signal-driven applications in Turmeric
since: signal v0.1.0
---

# tur-signal Guide

A synthesizer voice is a pipeline: an oscillator produces a waveform, a
filter shapes its spectrum, a shaper limits its amplitude, and an envelope
sculpts its loudness over time. This spice models that pipeline as typed
Signal Functions -- closures you compose left-to-right, sample at a time
you choose, and test without a sound card.

This guide walks the five things you will do most often:

1. [Signals and sampling](#1-signals)
2. [Oscillators: sine, square, sawtooth, triangle](#2-oscillators)
3. [Filters: low-pass and high-pass](#3-filters)
4. [Shapers and mixers](#4-shapers)
5. [ADSR envelopes and a complete voice](#5-envelopes)

Each section is a self-contained snippet you can drop into a `defmodule`.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric
(defpackage my-app
  :spices #{
    "signal" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "signal-v0.1.0"
               :subdir "spices/signal"}
  })
```

Then `tur fetch`. Pure Turmeric -- no C deps. Modules that call libm
(`signal/osc`, `signal/shaper`) carry a `__tur_autolink__: -lm` directive
so any program importing them links against the math library automatically.

---

## The one idea

A **`Signal A`** is conceptually a function from time to a value:
`(fn [t : float] A)`. The common case is `Signal Sample`, where a sample
is a `:float`.

An **`SF A B`** (Signal Function) maps a signal to a signal. Every
oscillator, filter, and shaper constructor returns an SF; apply it to a
signal to get a signal back:

```turmeric no-check
(let [tone   ((sine 440.0 0.0) (constant 0.0))   ;; SF applied -> Signal
      shaped ((gain 0.5) tone)]                  ;; SF applied -> Signal
  (sample shaped 0.0))                            ;; sample the result
```

`sample` evaluates a signal at a specific time. Because the whole graph is
pure functions, you can test it by sampling at known times and comparing to
hand-computed references -- no audio hardware, no real-time loop.

---

## 1. Signals

`constant` makes a signal that always emits the same value. `time-signal`
is the identity -- its value equals the query time. `sample` evaluates a
signal at a time you choose:

```turmeric
(defmodule demo
  (import signal/core :refer [constant time-signal sample])

  (defn main [] : int
    (let [c (constant 0.5)]
      (println (sample c 0.0)))         ;; 0.5
    (println (time-signal 2.0))          ;; 2.0
    0))
```

```sweet-exp
defmodule demo
  import signal/core :refer [constant time-signal sample]

  defn main [] : int
    let [c constant(0.5)]
      println $ sample c 0.0          ;; 0.5
    println $ time-signal 2.0         ;; 2.0
    0
```

`map-signal` lifts a pure function to operate on signals pointwise. It
returns a closure carried as a raw pointer, so re-bind it as `^fat` before
sampling:

```turmeric
(let [^fat m : (fn [float] float)
           (map-signal (fn [x : float] : float (* x 3.0)) (constant 2.0))]
  (sample m 0.0))                       ;; 6.0
```

---

## 2. Oscillators

Each oscillator is an `SF () Sample` -- it ignores its input signal. Apply
it to any signal (typically `(constant 0.0)`) to get a `Signal Sample`:

```turmeric
(defmodule demo
  (import signal/core :refer [constant sample])
  (import signal/osc  :refer [sine square sawtooth triangle])

  (defn main [] : int
    (let [s1 ((sine 1.0 0.0) (constant 0.0))]
      (println (sample s1 0.25)))       ;; ~1.0 (peak of a 1 Hz sine)
    (let [sq ((square 2.0 0.5) (constant 0.0))]
      (println (sample sq 0.1)))        ;; 1.0 (on-portion of duty cycle)
    (let [saw ((sawtooth 1.0) (constant 0.0))]
      (println (sample saw 0.5)))       ;; 0.5 (mid-ramp)
    (let [tri ((triangle 1.0) (constant 0.0))]
      (println (sample tri 0.25)))      ;; 0.0 (zero crossing)
    0))
```

```sweet-exp
defmodule demo
  import signal/core :refer [constant sample]
  import signal/osc  :refer [sine square sawtooth triangle]

  defn main [] : int
    let [s1 (sine(1.0 0.0))(constant(0.0))]
      println $ sample s1 0.25       ;; ~1.0 (peak of a 1 Hz sine)
    let [sq (square(2.0 0.5))(constant(0.0))]
      println $ sample sq 0.1        ;; 1.0 (on-portion of duty cycle)
    let [saw (sawtooth(1.0))(constant(0.0))]
      println $ sample saw 0.5       ;; 0.5 (mid-ramp)
    let [tri (triangle(1.0))(constant(0.0))]
      println $ sample tri 0.25       ;; 0.0 (zero crossing)
    0
```

| Oscillator | Parameters | Produces |
|-----------|-----------|----------|
| `sine` | freq, phase | `sin(2*pi*freq*t + phase)` |
| `square` | freq, dutycycle | +1 for the on-portion, -1 otherwise |
| `sawtooth` | freq | rising ramp in [0, 1) |
| `triangle` | freq | rises -1 to 1, falls 1 to -1 each cycle |

---

## 3. Filters

First-order IIR filters. Each application owns its own state cell, so the
same `(low-pass alpha)` SF is stateful from the moment it is applied to an
input signal. **Sample in order** -- the filter's output depends on the
previous sample.

```turmeric
(defmodule demo
  (import signal/core  :refer [constant sample])
  (import signal/osc   :refer [sine])
  (import signal/filter :refer [low-pass high-pass])

  (defn main [] : int
    (let [tone   ((sine 440.0 0.0) (constant 0.0))
          tone-f ((low-pass 0.3) tone)]
      (println (sample tone-f 0.0)))    ;; smoothed first sample
    0))
```

```sweet-exp
defmodule demo
  import signal/core  :refer [constant sample]
  import signal/osc   :refer [sine]
  import signal/filter :refer [low-pass high-pass]

  defn main [] : int
    let [tone   (sine(440.0 0.0))(constant(0.0))
          tone-f (low-pass(0.3))(tone)]
      println $ sample tone-f 0.0     ;; smoothed first sample
    0
```

| Filter | Formula | Notes |
|--------|---------|-------|
| `low-pass alpha` | `y = alpha*x + (1-alpha)*prev` | EMA; lower alpha = more smoothing |
| `high-pass alpha` | `x - low_pass(x)` | tracks an internal LP tap |

---

## 4. Shapers

Shapers are stateless sample-wise transforms. Some take a parameter
(`gain`, `offset`, `saturate-tanh`, `hard-clip`, `clip`); two are
captureless (`invert`, `abs-sf`) and take no parameters at all -- bind the
SF to a name first, then apply it:

```turmeric
(defmodule demo
  (import signal/core  :refer [constant sample])
  (import signal/shaper :refer [gain invert saturate-tanh hard-clip clip scale])

  (defn main [] : int
    (let [iv (invert)]
      (println (sample (iv (constant 0.5)) 0.0)))   ;; -0.5
    (let [g (gain 2.0)]
      (println (sample (g (constant 3.0)) 0.0)))    ;; 6.0
    (println (scale 0.0 1.0 -1.0 1.0 0.75))          ;; 0.5 (pure remap, not an SF)
    0))
```

```sweet-exp
defmodule demo
  import signal/core  :refer [constant sample]
  import signal/shaper :refer [gain invert saturate-tanh hard-clip clip scale]

  defn main [] : int
    let [iv invert()]
      println $ sample $ iv $ constant 0.5 $ 0.0   ;; -0.5
    let [g gain(2.0)]
      println $ sample $ g $ constant 3.0 $ 0.0   ;; 6.0
    println $ scale(0.0 1.0 -1.0 1.0 0.75)          ;; 0.5 (pure remap, not an SF)
    0
```

The Pair-consuming mixers (`mix`, `add`, `multiply`) consume the
`Signal (Pair Sample Sample)` shape produced by `pair-signals`:

```turmeric no-check
(let [mixed ((mix 0.5) (pair-signals sine-a sine-b))]  ;; alpha*x + (1-alpha)*y
  (sample mixed 0.0))
(let [ring  ((multiply) (pair-signals lfo carrier))]  ;; ring modulation
  (sample ring 0.0))
```

---

## 5. Envelopes

An ADSR envelope shapes amplitude over the lifetime of a note. Build the
parameters as a `:copy` struct with `make-struct`, then pass it by value to
`adsr-fixed`:

```turmeric
(defmodule demo
  (import signal/core     :refer [constant sample])
  (import signal/envelope :refer [ADSRParams adsr-fixed adsr-gen])

  (defn main [] : int
    (let [params (make-struct ADSRParams 0.01 0.1 0.7 0.3)
          env    ((adsr-fixed params 0.5) (constant 0.0))]
      (println (sample env 0.005)))     ;; ~0.5 (mid-attack)
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant sample]
  import signal/envelope :refer [ADSRParams adsr-fixed adsr-gen]

  defn main [] : int
    let [params make-struct(ADSRParams 0.01 0.1 0.7 0.3)
          env    (adsr-fixed(params 0.5))(constant(0.0))]
      println $ sample env 0.005      ;; ~0.5 (mid-attack)
    0
```

`adsr-fixed` takes a gate duration in seconds; `adsr-gen` is the
convenience that uses a unit (1.0s) gate.

### A complete voice

Oscillator through a gain and low-pass chain, multiplied by an envelope:

```turmeric
(defmodule demo
  (import signal/core     :refer [constant sample pair-signals])
  (import signal/osc      :refer [sine])
  (import signal/filter   :refer [low-pass])
  (import signal/shaper   :refer [gain multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (import signal/compose  :refer [effects-chain])

  (defn main [] : int
    (let [osc ((sine 2.0 0.0) (constant 0.0))
          ^fat chain : (fn [float] float
               (effects-chain (vec-of (gain 0.8) (low-pass 0.5)) osc)
          env ((adsr-fixed (make-struct ADSRParams 0.1 0.1 0.6 0.2) 0.5)
               (constant 0.0))
          ml  (multiply)
          ^fat voice : (fn [float] float (ml (pair-signals chain env))]
      (println (sample voice 0.05)))    ;; ~0.1176
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant sample pair-signals]
  import signal/osc      :refer [sine]
  import signal/filter   :refer [low-pass]
  import signal/shaper   :refer [gain multiply]
  import signal/envelope :refer [ADSRParams adsr-fixed]
  import signal/compose  :refer [effects-chain]

  defn main [] : int
    let [osc (sine(2.0 0.0))(constant(0.0))
          ^fat chain : (fn [float] float)
               effects-chain(vec-of(gain(0.8) low-pass(0.5)) osc)
          env (adsr-fixed(make-struct(ADSRParams 0.1 0.1 0.6 0.2) 0.5))
               constant(0.0)
          ml  multiply()
          ^fat voice : (fn [float] float ml(pair-signals(chain env))]
      println $ sample voice 0.05     ;; ~0.1176
    0
```

`effects-chain` applies a `Vec` of SFs left-to-right to an input signal. It
returns a closure carried as a raw pointer, so re-bind it as `^fat` before
sampling.

---

## Calling conventions worth knowing

- **Captureless shapers** (`invert`, `abs-sf`, `add`, `multiply`) take no
  parameters. Bind the SF to a name first, then apply it -- do not apply the
  bare `(invert)` result inline.
- **`map-signal` and `effects-chain`** return a closure carried as a raw
  pointer. Re-bind the result as a `^fat` signal before sampling.
- **`ADSRParams`** is a `:copy` struct; build it with `make-struct` and pass
  it by value to `adsr-fixed` / `adsr-gen`.

---

## When not to use this

- **You need wavetable, FM, Karplus-Strong, or granular oscillators.** Tier
  2 -- explicitly out of scope until each lands behind a real consumer and
  a dedicated plan.
- **You need a poly-synth, step-sequencer, or voice manager.** Those are
  Tier 2 composition layers.
- **You need resonant, state-variable, or ladder filters.** Only first-order
  IIR low-pass and high-pass ship in v0.

---

## Examples

Five runnable examples live under `examples/`:

```sh
cd spices/signal
tur run examples/01_constant_and_time.tur     # constant + time-signal
tur run examples/02_oscillators.tur           # sine/square/sawtooth/triangle
tur run examples/03_filters_and_shapers.tur   # filters + scalar shapers
tur run examples/04_envelopes.tur             # ADSR
tur run examples/05_simple_voice.tur          # capstone: every module wired together
```

Each prints sample values that match the hand-computed references in its
comments.

---

## Tests

```sh
cd spices/signal
tur test tests/signal    # 6 tests, all pass
```

---

## See also

- [API reference](api/)
- [README](../../spices/signal/README.md) -- the full module and symbol tables
- [tur-math guide](math-guide.html) -- scalar helpers (clamp, lerp, remap) used alongside DSP
