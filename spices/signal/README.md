# tur-signal

Typed Signal Function (SF) library for [Turmeric](https://github.com/turmeric-lang/turmeric):
oscillators (including FM and PM), one-pole / biquad / state-variable / ladder
filters, delays and combs, colored noise, sample buffers and buffer scanning,
shapers, ADSR envelopes, and SF-pipeline composition.

---

## Status

Runs end-to-end against turmeric `main` (checked with v0.64.1). All ten
modules `tur check` clean, all nine examples run and print the values in
their comments, and the full `tests/signal/` suite passes:

```
$ tur test tests/signal
PASS test_buffer
PASS test_compose
PASS test_core
PASS test_delay
PASS test_envelope
PASS test_filter
PASS test_hf_osc
PASS test_noise
PASS test_osc
PASS test_shaper
..........
10 tests, 10 passed, 0 failed
```

Not provided: Karplus-Strong and granular synthesis, band-limited wavetable
sets, saturating (analog-modeled) filters, voice management / poly-synths /
sequencers, and audio device or file output. The
[guide](https://spices.turmeric-lang.com/docs/html/guides/signal-guide.html)
walks every module with runnable examples.

---

## Concepts

A **`Signal A`** is conceptually a function from time to a value:
`(fn [t :float] A)`. The common case is `Signal Sample`, where a sample
is a `:float`: `(fn [t :float] :float)`.

An **`SF A B`** (Signal Function) maps a signal to a signal. Every
oscillator, filter, and shaper constructor returns an SF; apply it to a
signal to get a signal back:

```turmeric no-check
(let [tone   ((sine 440.0 0.0) (constant 0.0))   ;; SF applied -> Signal
      shaped ((gain 0.5) tone)]                   ;; SF applied -> Signal
  (sample shaped 0.0))                            ;; sample the result
```

The Pair-consuming mixers (`mix`, `add`, `multiply`) consume the
`Signal (Pair Sample Sample)` shape produced by `pair-signals`.

### Calling conventions worth knowing

A few constructors need a specific call shape:

- **Captureless shapers** (`invert`, `abs-sf`, `add`, `multiply`) take no
  parameters. Bind the SF to a name first, then apply it -- applying the
  bare `(invert)` result inline does not compile:

  ```turmeric no-check
  (let [iv (invert)]
    (sample (iv (constant 0.5)) 0.0))   ;; -0.5
  ```

- **`effects-chain`** returns the chained signal as a raw pointer. Re-bind
  the result as a `^fat` signal before sampling:

  ```turmeric no-check
  (let [^fat out : (fn [float] float)
             (effects-chain (vec-of (gain 0.5) (low-pass 0.3)) osc)]
    (sample out 0.0))
  ```

  `map-signal` returns an ordinary signal; sample it directly.

- **`ADSRParams`** is a `:copy` struct; build it with `make-struct` and
  pass it by value to `adsr-fixed` / `adsr-gen`.

- **Sample rates come first** in every constructor that takes one
  (`(biquad-lp 48000.0 1200.0 0.7071)`, `(delay 48000.0 0.25)`), except the
  `hf-osc` oscillators, which take it last.

---

## Module layout

| Module            | Surface                                                                                  |
|-------------------|------------------------------------------------------------------------------------------|
| `signal/core`     | `constant`, `time-signal`, `sample`, `map-signal`, `pair-signals`                        |
| `signal/osc`      | `sine`, `square`, `sawtooth`, `triangle`, `pm-sine`, `fm-sine`                           |
| `signal/hf-osc`   | `hf-sawtooth`, `hf-square`, `hf-triangle` -- band-limited, for hearing-range pitches     |
| `signal/filter`   | one-pole (`low-pass`, `high-pass`, `one-pole-lp`, `one-pole-hp`, `dc-block`), RBJ biquads (`biquad-*`, `biquad`), state-variable (`svf-*`), `ladder` |
| `signal/buffer`   | `Buffer`, `buffer-new`, `buffer-render`, `buffer-len`, `buffer-get`, `buffer-set!`, `buffer-read`, `scan` |
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
| `map-signal f sig` | `(fn [float] float), Signal -> Signal` | apply `f` pointwise; sample the result directly |
| `pair-signals a b` | `Signal, Signal -> Signal (Pair Sample Sample)` | zip two signals |

### `signal/osc`

The four shapes are `SF () Sample` -- they ignore their input signal.

| Symbol | Produces |
|---|---|
| `sine freq phase` | `sin(2*pi*freq*t + phase)` |
| `square freq duty` | `+1` for the on-portion of each cycle, `-1` otherwise |
| `sawtooth freq` | rising ramp in `[0, 1)` |
| `triangle freq` | rises `-1 -> 1` then falls `1 -> -1` each cycle |

These are the *naive* shapes: right for control signals (LFOs), but they alias
at audio rate. For hearing-range pitches use `signal/hf-osc`.

The two modulation oscillators are `SF Sample Sample`: their input signal *is*
the modulator.

| Symbol | Input | Produces |
|---|---|---|
| `pm-sine freq` | phase offset `m`, radians | `sin(2*pi*freq*t + m(t))`; pure. The modulator's peak is the modulation index (DX7-style "FM"). |
| `fm-sine sample-rate freq` | frequency deviation `m`, Hz | a sine at instantaneous frequency `freq + m(t)`, phase integrated per sample (trapezoidal); **stateful**. Negative frequencies run backwards (through-zero FM). |

For a sine modulator they are the same sound: FM by `D*cos(2*pi*fm*t)` is PM
by `(D/fm)*sin(2*pi*fm*t)`, which `test_osc` checks.

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

Every filter is stateful; each application owns its own state cell. Every
constructor that takes Hz takes the sample rate first. Cutoffs are clamped to
`[0, 0.49 * sample-rate]` and `q` to at least 0.01; a biquad given a
non-positive sample rate passes its input through.

| Symbol | Produces |
|---|---|
| `low-pass alpha` | EMA: `y = alpha*x + (1-alpha)*prev` |
| `high-pass alpha` | `x - low_pass(x)` |
| `one-pole-lp sr freq` / `one-pole-hp sr freq` | the same, with `alpha = 1 - exp(-2*pi*freq/sr)` |
| `dc-block sr freq` | `y = x - x[n-1] + r*y[n-1]`, `r = exp(-2*pi*freq/sr)`: removes DC |
| `biquad b0 b1 b2 a1 a2` | second-order section from raw `a0`-normalized coefficients |
| `biquad-lp sr freq q` / `biquad-hp sr freq q` | RBJ cookbook 12 dB/oct; gain `q` at `freq` |
| `biquad-bp sr freq q` | band-pass, 0 dB at `freq` |
| `biquad-notch sr freq q` | removes `freq` |
| `biquad-allpass sr freq q` | unit gain, 360 deg of phase turn around `freq` |
| `biquad-peak sr freq q gain-db` | peaking EQ |
| `biquad-low-shelf sr freq q gain-db` / `biquad-high-shelf ...` | shelving EQ |
| `svf-lp sr cutoff q` / `svf-hp` / `svf-bp` / `svf-notch` | trapezoidal state-variable filter (Simper); **`cutoff` is a signal**, read every sample, and stays stable while it moves. `svf-bp` is unity at the center. With a constant cutoff it equals the matching biquad. |
| `ladder sr cutoff resonance` | 4-pole 24 dB/oct low-pass, linear zero-delay-feedback (Zavalishin); `cutoff` is a signal; `resonance` in `[0, 1]`, self-oscillating at 1; DC gain `1 / (1 + 4*resonance)` |

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

### `signal/buffer`

A `Buffer` is a fixed-length array of samples (up to 16777216), shared by
handle and living for the process. `scan` reads one as a signal.

| Symbol | Produces |
|---|---|
| `buffer-new n` | `n` silent samples |
| `buffer-render sr n sig` | `n` samples of `sig` at `t = k / sr`, sampled in order |
| `buffer-len buf` | the length |
| `buffer-get buf i` / `buffer-set! buf i x` | one sample; the index wraps |
| `buffer-read buf pos` | linearly interpolated read at `pos` buffer lengths; wraps |
| `scan buf` | SF: input = read position in buffer lengths, output = the buffer there; stateless |

Scanned by a `sawtooth` at f Hz, a one-cycle buffer is an f Hz wavetable
oscillator; a recording scanned once per its duration plays back looped, and
faster or slower ramps transpose it like tape.

### Stateful signals are pull-order dependent

Every filter, every delay effect, every noise generator and `fm-sine` advance
their internal state once per `sample` call and ignore `t`. Sample each
instance exactly once per sample period, in order. Feeding one instance to two
consumers (for example both halves of `pair-signals`) advances it twice, so
each consumer sees only part of the stream and a delay's effective length
changes. Build one instance per consumer, or render the signal into a `Buffer` and
scan that. The other oscillators, the shapers, the envelopes and `scan` are
pure functions of `t` and have no such restriction.

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

```turmeric no-check
(defpackage my-app
  :spices #map{
    "signal" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                  :ref    "main"
                  :subdir "spices/signal"}
  })
```

Then `tur fetch`. There is no `signal-v*` release tag yet, so track `main`.

---

## Quick start

```turmeric
(defmodule my/app
  (import signal/core     :refer [constant sample pair-signals])
  (import signal/osc      :refer [sine])
  (import signal/filter   :refer [low-pass biquad-lp])
  (import signal/shaper   :refer [gain multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (import signal/compose  :refer [effects-chain])

  (defn main [] : int
    ;; A constant, and an oscillator sampled at its peak.
    (println (sample (constant 0.5) 0.0))               ;; 0.5
    (let [s1 ((sine 1.0 0.0) (constant 0.0))]
      (println (sample s1 0.25)))                       ;; 1

    ;; Filters are stateful: each sample call advances them.
    (let [lp ((biquad-lp 48000.0 1000.0 0.7071) (constant 1.0))]
      (println (sample lp 0.0))                         ;; 0.00391612
      (println (sample lp 0.0)))                        ;; 0.0188575

    ;; An ADSR envelope half way up its attack.
    (let [env ((adsr-fixed (make-struct ADSRParams 0.01 0.1 0.7 0.3) 0.5)
               (constant 0.0))]
      (println (sample env 0.005)))                     ;; 0.5

    ;; A simple voice: sine -> [gain, low-pass] -> * envelope.
    (let [osc   ((sine 2.0 0.0) (constant 0.0))
          ^fat chain : (fn [float] float)
                (effects-chain (vec-of (gain 0.8) (low-pass 0.5)) osc)
          env   ((adsr-fixed (make-struct ADSRParams 0.1 0.1 0.6 0.2) 0.5)
                 (constant 0.0))
          ml    (multiply)
          voice (ml (pair-signals chain env))]
      (println (sample voice 0.05)))                    ;; 0.117557
    0))
```

```sweet-exp
defmodule my/app
  import signal/core     :refer [constant sample pair-signals]
  import signal/osc      :refer [sine]
  import signal/filter   :refer [low-pass biquad-lp]
  import signal/shaper   :refer [gain multiply]
  import signal/envelope :refer [ADSRParams adsr-fixed]
  import signal/compose  :refer [effects-chain]

  defn main [] : int
    ;; A constant, and an oscillator sampled at its peak.
    println $ sample constant(0.5) 0.0                ;; 0.5
    let [s1 (sine(1.0 0.0) constant(0.0))]
      println $ sample s1 0.25                        ;; 1

    ;; Filters are stateful: each sample call advances them.
    let [lp (biquad-lp(48000.0 1000.0 0.7071) constant(1.0))]
      println $ sample lp 0.0                         ;; 0.00391612
      println $ sample lp 0.0                         ;; 0.0188575

    ;; An ADSR envelope half way up its attack.
    let [env (adsr-fixed(make-struct(ADSRParams 0.01 0.1 0.7 0.3) 0.5)
              constant(0.0))]
      println $ sample env 0.005                      ;; 0.5

    ;; A simple voice: sine -> [gain, low-pass] -> * envelope.
    let [osc   (sine(2.0 0.0) constant(0.0))
         ^fat chain : (fn [float] float)
               effects-chain(vec-of(gain(0.8) low-pass(0.5)) osc)
         env   (adsr-fixed(make-struct(ADSRParams 0.1 0.1 0.6 0.2) 0.5)
                 constant(0.0))
         ml    multiply()
         voice ml(pair-signals(chain env))]
      println $ sample voice 0.05                     ;; 0.117557
    0
```

---

## Examples

Nine runnable examples live under `examples/`:

```sh
cd spices/signal
tur run examples/01_constant_and_time.tur     # constant + time-signal
tur run examples/02_oscillators.tur           # sine/square/sawtooth/triangle
tur run examples/03_filters_and_shapers.tur   # filters + scalar shapers
tur run examples/04_envelopes.tur             # ADSR
tur run examples/05_simple_voice.tur          # capstone: every module wired together
tur run examples/06_hf_oscillators.tur        # naive vs band-limited sawtooth at 7.9 kHz
tur run examples/07_delay_and_noise.tur       # delay, comb, allpass, colored noise
tur run examples/08_filters.tur               # biquads, state-variable sweep, ladder
tur run examples/09_modulation_and_buffers.tur  # ring, AM, PM, FM, wavetable, tape
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

Modules that call libm (`signal/osc`, `signal/hf-osc`, `signal/filter`,
`signal/shaper`, `signal/buffer`) carry a `__tur_autolink__: -lm` directive
so any program importing them links against the math library automatically.

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/signal-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/signal>
