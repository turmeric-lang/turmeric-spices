# tur-signal

Typed Signal Function (SF) library for [Turmeric](https://github.com/turmeric-lang/turmeric):
oscillators, filters, shapers, ADSR envelopes, and SF-pipeline composition.

---

## Status

The Tier 1 surface ships and runs end-to-end against turmeric `main`.
All nine modules `tur check` clean, all seven examples run and print the
values in their comments, and the full `tests/signal/` suite passes:

```
$ tur test tests/signal
PASS test_compose
PASS test_core
PASS test_delay
PASS test_envelope
PASS test_filter
PASS test_hf_osc
PASS test_noise
PASS test_osc
PASS test_shaper
.........
9 tests, 9 passed, 0 failed
```

This is the surface area an honest version of the previous spice would
have shipped. Tier 2 -- wavetable / FM / Karplus-Strong / granular,
voice / poly-synth / step-sequencer, resonant filters -- is explicitly
out of scope until each lands behind a real consumer and a dedicated
plan. See `docs/upcoming/tur-signal-rebuild-plan.md` in the turmeric
repo.

---

## Concepts

A **`Signal A`** is conceptually a function from time to a value:
`(fn [t :float] A)`. The common case is `Signal Sample`, where a sample
is a `:float`: `(fn [t :float] :float)`.

An **`SF A B`** (Signal Function) maps a signal to a signal. Every
oscillator, filter, and shaper constructor returns an SF; apply it to a
signal to get a signal back:

```turmeric
(let [tone   ((sine 440.0 0.0) (constant 0.0))   ;; SF applied -> Signal
      shaped ((gain 0.5) tone)]                   ;; SF applied -> Signal
  (sample shaped 0.0))                            ;; sample the result
```

The Pair-consuming mixers (`mix`, `add`, `multiply`) consume the
`Signal (Pair Sample Sample)` shape produced by `pair-signals`.

### Calling conventions worth knowing

A few constructors need a specific call shape:

- **Captureless shapers** (`invert`, `abs-sf`, `add`, `multiply`) take no
  parameters. Bind the SF to a name first, then apply it -- don't apply
  the bare `(invert)` result inline:

  ```turmeric
  (let [iv (invert)]
    (sample (iv (constant 0.5)) 0.0))   ;; -0.5
  ```

- **`map-signal`** and **`effects-chain`** return a closure carried as a
  raw pointer. Re-bind the result as a `^fat` signal before sampling:

  ```turmeric
  (let [^fat m : (fn [float] float)
             (map-signal (fn [x :float] :float (* x 3.0)) (constant 2.0))]
    (sample m 0.0))                      ;; 6.0
  ```

- **`ADSRParams`** is a `:copy` struct; build it with `make-struct` and
  pass it by value to `adsr-fixed` / `adsr-gen`.

---

## Module layout

| Module            | Surface                                                                                  |
|-------------------|------------------------------------------------------------------------------------------|
| `signal/core`     | `constant`, `time-signal`, `sample`, `map-signal`, `pair-signals`                        |
| `signal/osc`      | `sine`, `square`, `sawtooth`, `triangle`                                                 |
| `signal/hf-osc`   | `hf-sawtooth`, `hf-square`, `hf-triangle` -- band-limited, for hearing-range pitches     |
| `signal/filter`   | `low-pass`, `high-pass`                                                                  |
| `signal/delay`    | `delay`, `comb-ff`, `comb`, `allpass` (+ sample-counted `*-n` forms) -- ring-buffer effects |
| `signal/noise`    | `white`, `pink`, `brown`, `blue`, `violet` (+ injected-source `*-from`, and `rng`)       |
| `signal/shaper`   | `gain`, `offset`, `invert`, `abs-sf`, `scale`, `saturate-tanh`, `hard-clip`, `clip`, `mix`, `add`, `multiply` |
| `signal/envelope` | `ADSRParams`, `adsr-fixed`, `adsr-gen`                                                   |
| `signal/compose`  | `effects-chain`                                                                          |

### `signal/core`

| Symbol | Signature | Produces |
|---|---|---|
| `constant val` | `:float -> Signal Sample` | a signal that always emits `val` |
| `time-signal t` | `:float -> :float` | the identity signal (value == query time) |
| `sample sig t` | `Signal Sample, :float -> :float` | evaluate `sig` at time `t` |
| `map-signal f sig` | `(fn [float] float), Signal -> Signal` | apply `f` pointwise |
| `pair-signals a b` | `Signal, Signal -> Signal (Pair Sample Sample)` | zip two signals |

### `signal/osc`

Each oscillator is an `SF () Sample` -- it ignores its input signal.

| Symbol | Produces |
|---|---|
| `sine freq phase` | `sin(2*pi*freq*t + phase)` |
| `square freq duty` | `+1` for the on-portion of each cycle, `-1` otherwise |
| `sawtooth freq` | rising ramp in `[0, 1)` |
| `triangle freq` | rises `-1 -> 1` then falls `1 -> -1` each cycle |

These are the *naive* shapes: right for control signals (LFOs), but they alias
at audio rate. For hearing-range pitches use `signal/hf-osc`.

### `signal/hf-osc`

Band-limited oscillators (PolyBLEP / PolyBLAMP) for hearing-range pitches. Each
takes the **sample rate** the signal will be sampled at, so it can size the edge
correction to one sample period. See the guide's section 2b for the measured
aliasing reduction.

| Symbol | Produces |
|---|---|
| `hf-sawtooth freq sample-rate` | rising ramp in `[-1, 1)` (bipolar), step smoothed |
| `hf-square freq duty sample-rate` | `+1` / `-1`, both edges smoothed |
| `hf-triangle freq sample-rate` | rises `-1 -> 1` then falls, corners smoothed |

### `signal/filter`

First-order IIR filters; each application owns its own state cell.

| Symbol | Produces |
|---|---|
| `low-pass alpha` | EMA: `y = alpha*x + (1-alpha)*prev` |
| `high-pass alpha` | `x - low_pass(x)` |

### `signal/delay`

Ring-buffer effects. Two layers: the `*-n` constructors take the delay in
**whole samples** and need no sample rate; the bare ones take
`(sample-rate seconds ...)` -- `sample-rate` in Hz, like `signal/hf-osc` -- and
round `seconds * sample-rate` to the nearest sample. Delays clamp to
`[1, 16777216]` samples.

| Symbol | Produces |
|---|---|
| `delay-n n` / `delay sr secs` | the input, `n` samples late (first `n` outputs are 0) |
| `comb-ff-n n g` / `comb-ff sr secs g` | feedforward comb: `y = x + g*x[n-D]` (always stable) |
| `comb-n n g` / `comb sr secs g` | feedback comb: `y = x + g*y[n-D]`; needs `|g| < 1` |
| `allpass-n n g` / `allpass sr secs g` | Schroeder allpass: `v = x + g*v[n-D]`, `y = v[n-D] - g*v`; needs `|g| < 1` |

Feedback gains are not clamped: `|g| >= 1` grows without bound.

### `signal/noise`

Generators: apply the SF to any signal (it is ignored), e.g.
`((pink 1) (constant 0.0))`. Output is always within `[-1, 1]`.

| Symbol | Spectrum | Method |
|---|---|---|
| `white seed` | flat | SplitMix64, uniform in `[-1, 1)` |
| `pink seed` | -3 dB/oct (1/f) | Kellet's 7-pole filter on white |
| `brown seed` | -6 dB/oct | leaky integral of white |
| `blue seed` | +3 dB/oct | first difference of pink |
| `violet seed` | +6 dB/oct | first difference of white |

Grey noise is a psychoacoustic equal-loudness curve rather than a slope and is
not provided.

**Purity.** `(white seed)` keeps its PRNG state in a private cell: its effect
row is `#fx{}` and a seed always yields the same samples. The `*-from`
constructors (`white-from`, `pink-from`, ...) instead take a source
`src : (fn [] #fx{e} float)` of raw white samples and return a signal whose row
is `#fx{e}`. Pass `(rng seed)` and it stays pure; pass an entropy read or a
live input and it is honestly `#fx{IO}`, and the compiler rejects using it
from a function declared `#fx{}` (`TUR-E0009`). Row checking needs a recent
compiler: turmeric v0.55 does not enforce it, v0.64 does.

### Stateful signals are pull-order dependent

`low-pass`, `high-pass`, every delay effect and every noise generator advance
their internal state once per `sample` call and ignore `t`. Sample each
instance exactly once per sample period, in order. Feeding one instance to two
consumers (for example both halves of `pair-signals`) advances it twice, so
each consumer sees only part of the stream and a delay's effective length
changes. Build one instance per consumer. The oscillators and shapers are pure
functions of `t` and have no such restriction.

### `signal/shaper`

| Symbol | Kind | Produces |
|---|---|---|
| `gain g` | SF | `g * x` |
| `offset c` | SF | `x + c` |
| `invert` | SF (captureless) | `-x` |
| `abs-sf` | SF (captureless) | `\|x\|` |
| `saturate-tanh drive` | SF | `tanh(drive*x) / tanh(drive)` |
| `hard-clip limit` | SF | clip to `[-limit, +limit]` |
| `clip lo hi` | SF | clip to `[lo, hi]` |
| `scale in-lo in-hi out-lo out-hi x` | pure `:float -> :float` | linear remap |
| `mix alpha` | SF over Pair | `alpha*x + (1-alpha)*y` |
| `add` | SF over Pair (captureless) | `x + y` |
| `multiply` | SF over Pair (captureless) | `x * y` |

### `signal/envelope`

| Symbol | Produces |
|---|---|
| `ADSRParams attack decay sustain release` | typed `:copy` struct |
| `adsr-fixed params gate-duration` | `SF () Sample` (analytic ADSR) |
| `adsr-gen params` | `adsr-fixed` with a unit (1.0s) gate |

### `signal/compose`

| Symbol | Produces |
|---|---|
| `effects-chain effects input` | applies a `Vec` of SFs left-to-right to `input` |

---

## Installation

In your `build.tur` `:spices` block:

```turmeric
(defpackage my-app
  :spices #{
    "signal" #{:url    "https://github.com/turmeric-lang/turmeric-spices"
               :ref    "signal-v0.1.0"
               :subdir "spices/signal"}
  })
```

Then `tur fetch`.

---

## Quick start

```turmeric
(defmodule my/app
  (import signal/core     :refer [constant sample pair-signals])
  (import signal/osc      :refer [sine])
  (import signal/filter   :refer [low-pass])
  (import signal/shaper   :refer [gain multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (import signal/compose  :refer [effects-chain]))

(defn main [] : int
  ;; A constant and the time identity.
  (let [c (constant 0.5)]
    (println (sample c 0.0)))                 ;; 0.5

  ;; An oscillator sampled at its peak.
  (let [s1 ((sine 1.0 0.0) (constant 0.0))]
    (println (sample s1 0.25)))               ;; ~1.0

  ;; A sine through a low-pass filter (stateful: sample in order).
  (let [tone   ((sine 440.0 0.0) (constant 0.0))
        tone-f ((low-pass 0.3) tone)]
    (println (sample tone-f 0.0)))

  ;; An ADSR envelope sampled mid-attack.
  (let [params (make-struct ADSRParams 0.01 0.1 0.7 0.3)
        env    ((adsr-fixed params 0.5) (constant 0.0))]
    (println (sample env 0.005)))             ;; ~0.5

  ;; A simple voice: sine -> [gain, low-pass] -> * envelope.
  (let [osc ((sine 2.0 0.0) (constant 0.0))
        ^fat chain : (fn [float] float)
             (effects-chain (vec-of (gain 0.8) (low-pass 0.5)) osc)
        env ((adsr-fixed (make-struct ADSRParams 0.1 0.1 0.6 0.2) 0.5)
             (constant 0.0))
        ml  (multiply)
        ^fat voice : (fn [float] float) (ml (pair-signals chain env))]
    (println (sample voice 0.05)))            ;; ~0.1176

  0)
```

---

## Examples

Seven runnable, per-phase examples live under `examples/`:

```sh
cd spices/signal
tur run examples/01_constant_and_time.tur     # constant + time-signal
tur run examples/02_oscillators.tur           # sine/square/sawtooth/triangle
tur run examples/03_filters_and_shapers.tur   # filters + scalar shapers
tur run examples/04_envelopes.tur             # ADSR
tur run examples/05_simple_voice.tur          # capstone: every module wired together
tur run examples/06_hf_oscillators.tur        # naive vs band-limited sawtooth at 7.9 kHz
tur run examples/07_delay_and_noise.tur      # delay, comb, allpass, colored noise
```

Each prints sample values that match the hand-computed references in its
comments.

---

## Development

```sh
cd spices/signal
for f in src/signal/*.tur; do tur check "$f"; done   # all clean
tur test tests/signal                                 # all pass
```

Modules that call libm (`signal/osc`, `signal/shaper`) carry a
`__tur_autolink__: -lm` directive so any program importing them links
against the math library automatically.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/signal-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/signal>
