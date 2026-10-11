---
title: Signal Processing with Typed Signal Functions
category: Audio
description: Oscillators (LFO, band-limited, FM and PM), one-pole, biquad, state-variable and ladder filters, delays and combs, colored noise, sample buffers and scanning, MIDI note/Hz conversion and sequencing, shapers, ADSR envelopes, and SF-pipeline composition -- a typed Signal/SF abstraction for building audio DSP graphs
audience: developers building audio synthesis, DSP pipelines, or signal-driven applications in Turmeric
since: signal v0.1.0
---

# tur-signal Guide

A synthesizer voice is a pipeline: an oscillator produces a waveform, a
filter shapes its spectrum, a shaper limits its amplitude, and an envelope
sculpts its loudness over time. This spice models that pipeline as typed
Signal Functions -- closures you compose left-to-right, sample at a time
you choose, and test without a sound card.

This guide walks the things you will do most often:

1. [Signals and sampling](#1-signals)
2. [Oscillators](#2-oscillators), and
   [band-limited oscillators for audio rate](#2b-band-limited-oscillators-hf-osc)
3. [Filters: one-pole, biquad, state-variable and ladder](#3-filters)
4. [Shapers and mixers](#4-shapers)
5. [ADSR envelopes and a complete voice](#5-envelopes)
6. [Delays, combs and allpasses](#6-delays-combs-and-allpasses)
7. [Noise](#7-noise)
8. [Modulation: ring, AM, PM and FM](#8-modulation)
9. [Buffers and buffer scanning](#9-buffers-and-buffer-scanning)
10. [Pitches, melodies and cycles](#10-pitches-melodies-and-cycles)

Every example is a complete program: save it inside a project that depends
on the spice and `tur run` it. The comments show what it prints.

---

## 0. Installing the spice

In your project's `build.tur`:

```turmeric no-check
(defpackage my-app
  :spices #map{
    "signal" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                  :ref    "main"
                  :subdir "spices/signal"}
  })
```

Then `tur fetch`. There is no `signal-v*` release tag yet, so track `main`.
Pure Turmeric -- no C dependencies. The modules that call libm carry a
`__tur_autolink__: -lm` directive, so a program importing them links the
math library automatically.

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
functions, you can test it by sampling at known times and comparing to
hand-computed references -- no audio hardware, no real-time loop.

Two kinds of signal behave differently, and it matters everywhere below:

- **Pure** signals -- `constant`, the oscillators except `fm-sine`, the
  shapers, the envelopes, `scan` -- are functions of `t` and nothing else.
  Sample them at any time, in any order, as often as you like, and share one
  between several consumers.
- **Stateful** signals -- every filter, delay and noise generator, and
  `fm-sine` -- advance once per `sample` call and ignore `t`. See
  [Stateful signals and sample order](#stateful-signals-and-sample-order).

---

## 1. Signals

`constant` makes a signal that always emits the same value. `time-signal`
is the identity -- its value equals the query time. `sample` evaluates a
signal at a time you choose, and `map-signal` lifts a function on samples to
a function on signals. Any `(fn [t : float] : float ...)` is a signal too, so
a signal you need and the library lacks is one lambda away:

```turmeric
(defmodule demo
  (import signal/core :refer [constant time-signal sample map-signal])

  (defn main [] : int
    (println (sample (constant 0.5) 0.0))      ;; 0.5
    (println (time-signal 2.0))                ;; 2
    (let [tripled (map-signal (fn [x : float] : float (* x 3.0)) (constant 2.5))]
      (println (sample tripled 0.0)))          ;; 7.5
    (let [ramp (fn [t : float] : float (* 2.0 t))]
      (println (sample ramp 1.25)))            ;; 2.5
    0))
```

```sweet-exp
defmodule demo
  import signal/core :refer [constant time-signal sample map-signal]

  defn main [] : int
    println $ sample constant(0.5) 0.0        ;; 0.5
    println $ time-signal 2.0                 ;; 2
    let [tripled map-signal((fn [x : float] : float {x * 3.0}) constant(2.5))]
      println $ sample tripled 0.0            ;; 7.5
    let [ramp (fn [t : float] : float {2.0 * t})]
      println $ sample ramp 1.25              ;; 2.5
    0
```

---

## 2. Oscillators

Each oscillator is an `SF () Sample` -- it ignores its input signal. Apply
it to any signal (typically `(constant 0.0)`) to get a `Signal Sample`. The
pitches here are notes you can hear -- 440 Hz is A4, 220 Hz is A3, 110 Hz is
A2 ([section 10](#10-pitches-melodies-and-cycles) converts between note
numbers and Hz) -- so each is sampled at a fraction of its own period:

```turmeric
(defmodule demo
  (import signal/core :refer [constant sample])
  (import signal/osc  :refer [sine square sawtooth triangle])

  (defn main [] : int
    (let [z   (constant 0.0)
          s1  ((sine 440.0 0.0) z)
          sq  ((square 220.0 0.5) z)
          saw ((sawtooth 110.0) z)
          tri ((triangle 220.0) z)]
      (println (sample s1 (/ 0.25 440.0)))    ;; 1 (a quarter cycle in: the peak)
      (println (sample sq (/ 0.25 220.0)))    ;; 1 (inside the on-portion)
      (println (sample saw (/ 0.5 110.0)))    ;; 0.5 (half a cycle in: mid-ramp)
      (println (sample tri (/ 0.25 220.0))))  ;; 0 (a quarter cycle in: zero crossing)
    0))
```

```sweet-exp
defmodule demo
  import signal/core :refer [constant sample]
  import signal/osc  :refer [sine square sawtooth triangle]

  defn main [] : int
    let [z   constant(0.0)
         s1  (sine(440.0 0.0) z)
         sq  (square(220.0 0.5) z)
         saw (sawtooth(110.0) z)
         tri (triangle(220.0) z)]
      println $ sample s1 {0.25 / 440.0}     ;; 1 (a quarter cycle in: the peak)
      println $ sample sq {0.25 / 220.0}     ;; 1 (inside the on-portion)
      println $ sample saw {0.5 / 110.0}     ;; 0.5 (half a cycle in: mid-ramp)
      println $ sample tri {0.25 / 220.0}    ;; 0 (a quarter cycle in: zero crossing)
    0
```

| Oscillator | Parameters | Produces |
|-----------|-----------|----------|
| `sine` | freq, phase | `sin(2*pi*freq*t + phase)` |
| `square` | freq, dutycycle | +1 for the on-portion, -1 otherwise |
| `sawtooth` | freq | rising ramp in [0, 1) |
| `triangle` | freq | rises -1 to 1, falls 1 to -1 each cycle |

These four are the naive shapes. A sine is clean at any pitch, but the square,
sawtooth and triangle alias at audio pitches like these -- the values above are
exact, the sound is not. For a voice, use their band-limited twins in
[section 2b](#2b-band-limited-oscillators-hf-osc); the naive ones are right for
LFOs and other control signals.

`signal/osc` also has two oscillators that *read* their input signal as a
modulator, `pm-sine` and `fm-sine`; they are in [section 8](#8-modulation).

---

## 2b. Band-limited oscillators (`hf-osc`)

The oscillators above are the *naive* shapes, and that is the right thing for
a control signal: a 2 Hz sawtooth modulating a filter has nothing near the
Nyquist frequency to alias. At **audio rate** it is the wrong thing. A bare
ramp or comparison has sharp edges, so its harmonics run past half the sample
rate and fold back down as inharmonic partials -- the harsh, metallic "digital"
sound -- that no later filter can remove, because by then they sit at the same
frequencies as the real harmonics' neighbours.

`signal/hf-osc` is the same three shapes for hearing-range pitches. It adds a
small polynomial correction around each edge (PolyBLEP for the square and
sawtooth, PolyBLAMP for the triangle's corners) so the edge is rounded the way
a band-limited signal's would be. A sine needs none -- it has a single partial
-- so `signal/osc`'s `sine` is already fine at any pitch.

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/osc    :refer [sawtooth])
  (import signal/hf-osc :refer [hf-sawtooth hf-square])

  (defn main [] : int
    (let [z     (constant 0.0)
          ;; An LFO: the naive ramp is fine at 2 Hz.
          lfo   ((sawtooth 2.0) z)
          ;; A 440 Hz voice, to be sampled at 48 kHz: band-limit it.
          voice ((hf-sawtooth 440.0 48000.0) z)
          pulse ((hf-square 220.0 0.25 48000.0) z)]
      (println (sample lfo 0.25))                      ;; 0.5
      (println (sample voice 0.0005681818181818182))   ;; -0.5 (a quarter cycle)
      (println (sample pulse 0.0005)))                 ;; 1 (inside the 25% on-portion)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/osc    :refer [sawtooth]
  import signal/hf-osc :refer [hf-sawtooth hf-square]

  defn main [] : int
    let [z     constant(0.0)
         lfo   (sawtooth(2.0) z)
         voice (hf-sawtooth(440.0 48000.0) z)
         pulse (hf-square(220.0 0.25 48000.0) z)]
      println $ sample lfo 0.25                       ;; 0.5
      println $ sample voice 0.0005681818181818182    ;; -0.5 (a quarter cycle)
      println $ sample pulse 0.0005                   ;; 1 (inside the 25% on-portion)
    0
```

| Oscillator | Parameters | Produces |
|-----------|-----------|----------|
| `hf-sawtooth` | freq, sample-rate | rising ramp in **[-1, 1)**, step smoothed |
| `hf-square` | freq, dutycycle, sample-rate | +1 / -1, both edges smoothed |
| `hf-triangle` | freq, sample-rate | rises -1 to 1, falls back, corners smoothed |

Three things differ from `signal/osc`:

- **Pass the sample rate**, and sample on that grid (`t = n / sample-rate`).
  The correction is sized to one sample period, so it needs to know it. Sampled
  at a different rate than you declared, the signal is still a signal but
  aliases like a naive shape. A rate of `0` (or less) disables the correction
  and gives the naive shape rather than dividing by zero.
- **`hf-sawtooth` is bipolar**, `[-1, 1)`. `sawtooth` is a unipolar `[0, 1)`
  control ramp; an audio oscillator must not carry a DC offset.
- **Values can overshoot +/-1 slightly** around an edge, as any band-limited
  square or sawtooth does.

How much it helps, measured as aliased energy relative to the true harmonics
(FFT, 48 kHz; lower is better):

| | 3.0 kHz naive | 3.0 kHz `hf-` | 7.9 kHz naive | 7.9 kHz `hf-` |
|---|---|---|---|---|
| sawtooth | -13 dB | -27 dB | -7 dB | -27 dB |
| square | -16 dB | -36 dB | -10 dB | -38 dB |
| triangle | -40 dB | -48 dB | -26 dB | -45 dB |

That is a cheap two-sample correction, not a brick-wall filter. It removes
most of the audible aliasing for a few multiplies per sample, and pitches above
roughly a quarter of the sample rate get increasingly impure. For a cleaner
oscillator at a known pitch, render one cycle holding only the harmonics below
Nyquist into a buffer and scan it ([section 9](#9-buffers-and-buffer-scanning)).

---

## 3. Filters

Four families, all stateful (each application of a filter SF owns its own
state, so **sample it once per sample period, in order**):

| Family | Constructors | Slope | Cutoff |
|---|---|---|---|
| one-pole | `low-pass`, `high-pass` (coefficient); `one-pole-lp`, `one-pole-hp`, `dc-block` (Hz) | 6 dB/oct | fixed |
| biquad (RBJ cookbook) | `biquad-lp`, `-hp`, `-bp`, `-notch`, `-allpass`, `-peak`, `-low-shelf`, `-high-shelf`; `biquad` (raw coefficients) | 12 dB/oct | fixed |
| state-variable (TPT) | `svf-lp`, `svf-hp`, `svf-bp`, `svf-notch` | 12 dB/oct | **a signal** |
| ladder (ZDF) | `ladder` | 24 dB/oct | **a signal** |

Everything that takes Hz takes the **sample rate first**, like `signal/delay`:
`(biquad-lp 48000.0 1200.0 0.7071)`. Sample the result on that grid,
`t = n / sample-rate`. Cutoffs are clamped to `[0, 0.49 * sample-rate]` and
`q` to at least 0.01; a biquad given a non-positive sample rate passes its
input through unchanged.

### One-pole filters and the DC blocker

The two original filters take a smoothing coefficient: `low-pass alpha` is
the exponential moving average `y = alpha*x + (1-alpha)*y[n-1]`, and
`high-pass alpha` is `x - low-pass(x)`. `one-pole-lp` and `one-pole-hp` are
the same filters with a cutoff in Hz. `dc-block` removes a constant offset --
use it after anything that leaves one behind (a rectifier, an asymmetric clip,
ring modulation of an offset signal); 5 to 20 Hz is a typical corner.

Feeding a filter a constant 1.0 (a step) and sampling it repeatedly walks its
step response -- the time argument is ignored, the call count is not:

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/filter :refer [low-pass one-pole-lp dc-block])

  (defn main [] : int
    (let [lp ((low-pass 0.5) (constant 1.0))]
      (println (sample lp 0.0))     ;; 0.5
      (println (sample lp 0.0))     ;; 0.75
      (println (sample lp 0.0)))    ;; 0.875
    (let [hz ((one-pole-lp 48000.0 1000.0) (constant 1.0))]
      (println (sample hz 0.0)))    ;; 0.122694 (alpha for 1 kHz at 48 kHz)
    (let [dc ((dc-block 48000.0 10.0) (constant 1.0))]
      (println (sample dc 0.0))     ;; 1
      (println (sample dc 0.0)))    ;; 0.998692 (and on down toward 0)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/filter :refer [low-pass one-pole-lp dc-block]

  defn main [] : int
    let [lp (low-pass(0.5) constant(1.0))]
      println $ sample lp 0.0      ;; 0.5
      println $ sample lp 0.0      ;; 0.75
      println $ sample lp 0.0      ;; 0.875
    let [hz (one-pole-lp(48000.0 1000.0) constant(1.0))]
      println $ sample hz 0.0      ;; 0.122694 (alpha for 1 kHz at 48 kHz)
    let [dc (dc-block(48000.0 10.0) constant(1.0))]
      println $ sample dc 0.0      ;; 1
      println $ sample dc 0.0      ;; 0.998692 (and on down toward 0)
    0
```

### Biquads, and measuring a filter

The `biquad-*` constructors are the Audio-EQ-Cookbook designs (Robert
Bristow-Johnson): second-order sections whose coefficients are fixed when the
filter is built.

| Constructor | Parameters | Response |
|---|---|---|
| `biquad-lp` / `biquad-hp` | sr, freq, q | 12 dB/oct; gain `q` at `freq` (0.7071 is maximally flat, -3 dB) |
| `biquad-bp` | sr, freq, q | band-pass, 0 dB at `freq`; higher `q` is narrower |
| `biquad-notch` | sr, freq, q | removes `freq` |
| `biquad-allpass` | sr, freq, q | unit gain everywhere, phase turns through 360 deg around `freq` |
| `biquad-peak` | sr, freq, q, gain-db | boost or cut `gain-db` around `freq` |
| `biquad-low-shelf` / `biquad-high-shelf` | sr, freq, q, gain-db | boost or cut everything below / above `freq` |
| `biquad` | b0, b1, b2, a1, a2 | raw coefficients, `a0 = 1` |

Because a signal is a function you can sample, a filter's frequency response is
a few lines of code away: run a sine through it, let it settle, and measure the
level. `level` below does that; the printed values are the filters' design
gains:

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/osc    :refer [sine])
  (import signal/filter :refer [biquad-lp biquad-hp biquad-bp biquad-notch
                                biquad-peak svf-lp ladder])
  (load "stdlib/math.tur")   ;; sqrt

  ;; level -- amplitude of a filtered tone at 48 kHz: let it settle for 0.1 s,
  ;; then measure sqrt(2 * mean(y^2)) over the next 0.1 s.
  (defn level [^fat y : (fn [float] float)] : float
    (let [^mut k   0
          ^mut acc 0.0]
      (while (< k 9600)
        (let [v (sample y (/ (as float k) 48000.0))]
          (when (>= k 4800) (set! acc (+ acc (* v v)))))
        (set! k (+ k 1)))
      (sqrt (/ acc 2400.0))))

  (defn main [] : int
    (let [tone ((sine 1000.0 0.0) (constant 0.0))
          at1k (constant 1000.0)]
      (println (level ((biquad-lp 48000.0 1000.0 0.7071) tone)))    ;; 0.7071 (-3 dB)
      (println (level ((biquad-lp 48000.0 1000.0 4.0) tone)))       ;; 4 (gain q at the cutoff)
      (println (level ((biquad-lp 48000.0 250.0 0.7071) tone)))     ;; 0.0622119 (2 octaves up: -24 dB)
      (println (level ((biquad-hp 48000.0 250.0 0.7071) tone)))     ;; 0.998062
      (println (level ((biquad-bp 48000.0 1000.0 4.0) tone)))       ;; 1
      (println (level ((biquad-notch 48000.0 1000.0 4.0) tone)))    ;; 6.64907e-14
      (println (level ((biquad-peak 48000.0 1000.0 1.0 6.0) tone))) ;; 1.99526 (+6 dB)
      (println (level ((svf-lp 48000.0 at1k 4.0) tone)))            ;; 4 (same as biquad-lp)
      (println (level ((ladder 48000.0 at1k 0.0) tone))))           ;; 0.25 (-12 dB: four poles)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/osc    :refer [sine]
  import signal/filter :refer [biquad-lp biquad-hp biquad-bp biquad-notch
                               biquad-peak svf-lp ladder]
  load "stdlib/math.tur"   ;; sqrt

  ;; level -- amplitude of a filtered tone at 48 kHz: let it settle for 0.1 s,
  ;; then measure sqrt(2 * mean(y^2)) over the next 0.1 s.
  defn level [^fat y : (fn [float] float)] : float
    let [^mut k   0
         ^mut acc 0.0]
      while {k < 9600}
        let [v sample(y {as(float k) / 48000.0})]
          when {k >= 4800} set!(acc {acc + {v * v}})
        set! k {k + 1}
      sqrt {acc / 2400.0}

  defn main [] : int
    let [tone (sine(1000.0 0.0) constant(0.0))
         at1k constant(1000.0)]
      println $ level (biquad-lp(48000.0 1000.0 0.7071) tone)     ;; 0.7071 (-3 dB)
      println $ level (biquad-lp(48000.0 1000.0 4.0) tone)        ;; 4 (gain q at the cutoff)
      println $ level (biquad-lp(48000.0 250.0 0.7071) tone)      ;; 0.0622119 (2 octaves up: -24 dB)
      println $ level (biquad-hp(48000.0 250.0 0.7071) tone)      ;; 0.998062
      println $ level (biquad-bp(48000.0 1000.0 4.0) tone)        ;; 1
      println $ level (biquad-notch(48000.0 1000.0 4.0) tone)     ;; 6.64907e-14
      println $ level (biquad-peak(48000.0 1000.0 1.0 6.0) tone)  ;; 1.99526 (+6 dB)
      println $ level (svf-lp(48000.0 at1k 4.0) tone)             ;; 4 (same as biquad-lp)
      println $ level (ladder(48000.0 at1k 0.0) tone)             ;; 0.25 (-12 dB: four poles)
    0
```

`tone` feeds nine filters here. That is fine because an oscillator is pure;
each filter is a fresh instance with its own state. Feeding one *filter* to
nine consumers would not be.

### State-variable and ladder filters: the cutoff is a signal

A biquad's coefficients are computed once. Recomputing them every sample to
sweep the cutoff works badly: the direct form's stored state was produced by
the old coefficients, so a moving cutoff clicks, and a fast resonant sweep can
go unstable. The
state-variable filter is built to be swept: it is a trapezoidal integrator
network (Andrew Simper's form of the topology-preserving SVF) whose state stays
meaningful however the cutoff moves. So **its cutoff is a signal**, read every
sample at the same `t` as the input. Pass `(constant hz)` to hold it; pass an
LFO, an envelope or any lambda to move it.

`ladder` is the Moog topology: four one-poles in a row with the output fed
back to the input, solved without a unit delay in the loop (Vadim Zavalishin's
zero-delay-feedback ladder). It is 24 dB/oct, its cutoff is a signal too, and
its `resonance` runs from 0 to 1. It is the *linear* model -- no saturation --
so its level does not depend on the input's, and the passband drops as the
resonance rises (DC gain `1 / (1 + 4*resonance)`; follow it with `gain` to
make that up). At resonance 1 it self-oscillates.

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/osc    :refer [sine])
  (import signal/filter :refer [svf-bp ladder])
  (load "stdlib/math.tur")   ;; sqrt

  ;; quarters -- render 1 s at 48 kHz, in order, and print the level of each
  ;; quarter second.
  (defn quarters [^fat y : (fn [float] float)] : int
    (let [^mut k   0
          ^mut acc 0.0]
      (while (< k 48000)
        (let [v (sample y (/ (as float k) 48000.0))]
          (set! acc (+ acc (* v v))))
        (set! k (+ k 1))
        (when (= (mod k 12000) 0)
          (println (sqrt (/ acc 6000.0)))
          (set! acc 0.0)))
      0))

  (defn main [] : int
    ;; A band-pass whose center is a ramp from 500 Hz to 4.5 kHz over one
    ;; second, swept across a steady 2 kHz tone. The tone comes through in the
    ;; quarter where the band passes 2 kHz.
    (let [tone   ((sine 2000.0 0.0) (constant 0.0))
          rising (fn [t : float] : float (+ 500.0 (* 4000.0 t)))]
      (quarters ((svf-bp 48000.0 rising 8.0) tone)))
    ;; prints 0.10482, 0.571767, 0.162979, 0.0827114

    ;; At resonance 1 the ladder is an oscillator: kick it once and it rings
    ;; at its cutoff at a constant level, for good.
    (let [kick (fn [t : float] : float (if (< t 0.00001) 1.0 0.0))]
      (quarters ((ladder 48000.0 (constant 1000.0) 1.0) kick)))
    ;; prints 0.0230724, 0.023074, 0.023074, 0.023074
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/osc    :refer [sine]
  import signal/filter :refer [svf-bp ladder]
  load "stdlib/math.tur"   ;; sqrt

  ;; quarters -- render 1 s at 48 kHz, in order, and print the level of each
  ;; quarter second.
  defn quarters [^fat y : (fn [float] float)] : int
    let [^mut k   0
         ^mut acc 0.0]
      while {k < 48000}
        let [v sample(y {as(float k) / 48000.0})]
          set! acc {acc + {v * v}}
        set! k {k + 1}
        when {mod(k 12000) = 0}
          println $ sqrt {acc / 6000.0}
          set! acc 0.0
      0

  defn main [] : int
    ;; A band-pass whose center is a ramp from 500 Hz to 4.5 kHz over one
    ;; second, swept across a steady 2 kHz tone. The tone comes through in the
    ;; quarter where the band passes 2 kHz.
    let [tone   (sine(2000.0 0.0) constant(0.0))
         rising (fn [t : float] : float {500.0 + {4000.0 * t}})]
      quarters (svf-bp(48000.0 rising 8.0) tone)
    ;; prints 0.10482, 0.571767, 0.162979, 0.0827114

    ;; At resonance 1 the ladder is an oscillator: kick it once and it rings
    ;; at its cutoff at a constant level, for good.
    let [kick (fn [t : float] : float (if {t < 0.00001} 1.0 0.0))]
      quarters (ladder(48000.0 constant(1000.0) 1.0) kick)
    ;; prints 0.0230724, 0.023074, 0.023074, 0.023074
    0
```

| Constructor | Parameters | Response |
|---|---|---|
| `svf-lp` / `svf-hp` | sr, cutoff signal, q | 12 dB/oct; gain `q` at the cutoff |
| `svf-bp` | sr, cutoff signal, q | band-pass, 0 dB at the center |
| `svf-notch` | sr, cutoff signal, q | removes the center |
| `ladder` | sr, cutoff signal, resonance | 24 dB/oct low-pass; self-oscillates at resonance 1 |

With a constant cutoff, `svf-lp` and `biquad-lp` are the same filter (both
are the bilinear transform of one analog prototype, prewarped at the cutoff)
and agree sample for sample; the test suite checks that. Reach for the biquads
for fixed EQ, and for the SVF or ladder when anything moves.

---

## 4. Shapers

Shapers are stateless sample-wise transforms. Some take a parameter
(`gain`, `offset`, `saturate-tanh`, `hard-clip`, `clip`); two are
captureless (`invert`, `abs-sf`) and take no parameters at all -- bind the
SF to a name first, then apply it:

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/shaper :refer [gain invert scale])

  (defn main [] : int
    (let [iv (invert)]
      (println (sample (iv (constant 0.5)) 0.0)))   ;; -0.5
    (let [g (gain 2.0)]
      (println (sample (g (constant 3.0)) 0.0)))    ;; 6
    (println (scale 0.0 1.0 -1.0 1.0 0.75))         ;; 0.5 (pure remap, not an SF)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/shaper :refer [gain invert scale]

  defn main [] : int
    let [iv invert()]
      println $ sample (iv constant(0.5)) 0.0    ;; -0.5
    let [g gain(2.0)]
      println $ sample (g constant(3.0)) 0.0     ;; 6
    println $ scale 0.0 1.0 -1.0 1.0 0.75        ;; 0.5 (pure remap, not an SF)
    0
```

| Shaper | Produces |
|---|---|
| `gain g` | `g * x` |
| `offset c` | `x + c` |
| `invert` | `-x` |
| `abs-sf` | `\|x\|` |
| `saturate-tanh drive` | `tanh(drive*x) / tanh(drive)` -- soft clipping |
| `hard-clip limit` | clip to `[-limit, +limit]` |
| `clip lo hi` | clip to `[lo, hi]` |
| `scale in-lo in-hi out-lo out-hi x` | linear remap of a single value (not an SF) |

The Pair-consuming mixers (`mix`, `add`, `multiply`) consume the
`Signal (Pair Sample Sample)` shape produced by `pair-signals`; `add` and
`multiply` are captureless, like `invert`:

```turmeric no-check
(let [mixed ((mix 0.5) (pair-signals sine-a sine-b))]  ;; alpha*x + (1-alpha)*y
  (sample mixed 0.0))
(let [ml   (multiply)
      ring (ml (pair-signals carrier modulator))]      ;; ring modulation (section 8)
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
  (import signal/envelope :refer [ADSRParams adsr-fixed])

  (defn main [] : int
    (let [params (make-struct ADSRParams 0.01 0.1 0.7 0.3)
          env    ((adsr-fixed params 0.5) (constant 0.0))]
      (println (sample env 0.005)))     ;; 0.5 (half way up the attack)
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant sample]
  import signal/envelope :refer [ADSRParams adsr-fixed]

  defn main [] : int
    let [params make-struct(ADSRParams 0.01 0.1 0.7 0.3)
         env    (adsr-fixed(params 0.5) constant(0.0))]
      println $ sample env 0.005      ;; 0.5 (half way up the attack)
    0
```

`ADSRParams` is attack, decay, sustain level, release. `adsr-fixed` takes a
gate duration in seconds; `adsr-gen` is the convenience that uses a unit
(1.0 s) gate. The envelope is computed analytically from `t`, so it is pure.

### A complete voice

An A3 (220 Hz) sawtooth through a gain and a 2 kHz low-pass, multiplied by
the envelope of a half-second note. The low-pass is stateful, so the note is
rendered on the 48 kHz grid with `buffer-render` ([section 9](#9-buffers-and-buffer-scanning)),
and `peak` reads its level back at four points in its life:

```turmeric
(defmodule demo
  (import signal/core     :refer [constant pair-signals])
  (import signal/hf-osc   :refer [hf-sawtooth])
  (import signal/filter   :refer [biquad-lp])
  (import signal/shaper   :refer [gain multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (import signal/compose  :refer [effects-chain])
  (import signal/buffer   :refer [Buffer buffer-render buffer-get])

  ;; peak -- the largest |sample| of `b` in [from, to).
  (defn peak [b : Buffer from : int to : int] : float
    (let [^mut i from
          ^mut m 0.0]
      (while (< i to)
        (let [v (buffer-get b i)
              a (if (< v 0.0) (- 0.0 v) v)]
          (when (> a m) (set! m a)))
        (set! i (+ i 1)))
      m))

  (defn main [] : int
    (let [osc   ((hf-sawtooth 220.0 48000.0) (constant 0.0))
          ^fat chain : (fn [float] float)
                (effects-chain (vec-of (gain 0.8) (biquad-lp 48000.0 2000.0 0.7071)) osc)
          env   ((adsr-fixed (make-struct ADSRParams 0.01 0.1 0.6 0.2) 0.5)
                 (constant 0.0))
          ml    (multiply)
          voice (ml (pair-signals chain env))
          note  (buffer-render 48000.0 36000 voice)]   ;; 0.75 s: gate + release
      (println (peak note 0 240))          ;; 0.385413 (the first 5 ms: rising)
      (println (peak note 12000 14400))    ;; 0.474386 (0.25 .. 0.3 s: sustain)
      (println (peak note 28800 31200))    ;; 0.236298 (0.6 .. 0.65 s: releasing)
      (println (peak note 33600 36000)))   ;; 9.9799e-17 (0.7 .. 0.75 s: done)
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant pair-signals]
  import signal/hf-osc   :refer [hf-sawtooth]
  import signal/filter   :refer [biquad-lp]
  import signal/shaper   :refer [gain multiply]
  import signal/envelope :refer [ADSRParams adsr-fixed]
  import signal/compose  :refer [effects-chain]
  import signal/buffer   :refer [Buffer buffer-render buffer-get]

  ;; peak -- the largest |sample| of `b` in [from, to).
  defn peak [b : Buffer from : int to : int] : float
    let [^mut i from
         ^mut m 0.0]
      while {i < to}
        let [v buffer-get(b i)
             a (if {v < 0.0} {0.0 - v} v)]
          when {a > m} set!(m a)
        set! i {i + 1}
      m

  defn main [] : int
    let [osc   (hf-sawtooth(220.0 48000.0) constant(0.0))
         ^fat chain : (fn [float] float)
               effects-chain(vec-of(gain(0.8) biquad-lp(48000.0 2000.0 0.7071)) osc)
         env   (adsr-fixed(make-struct(ADSRParams 0.01 0.1 0.6 0.2) 0.5)
                 constant(0.0))
         ml    multiply()
         voice ml(pair-signals(chain env))
         note  buffer-render(48000.0 36000 voice)]   ;; 0.75 s: gate + release
      println $ peak note 0 240           ;; 0.385413 (the first 5 ms: rising)
      println $ peak note 12000 14400     ;; 0.474386 (0.25 .. 0.3 s: sustain)
      println $ peak note 28800 31200     ;; 0.236298 (0.6 .. 0.65 s: releasing)
      println $ peak note 33600 36000     ;; 9.9799e-17 (0.7 .. 0.75 s: done)
    0
```

The sustain level is the filtered sawtooth's peak (about 0.99 after the
low-pass) times the gain, 0.8, times the sustain, 0.6. `effects-chain` applies
a `Vec` of SFs left-to-right to an input signal. It returns the chained signal
as a raw pointer, so bind the result as a `^fat` signal (as `chain` is here)
before using it.

---

## 6. Delays, combs and allpasses

`signal/delay` holds ring-buffer effects. Two layers: the `*-n` constructors
take the delay in **whole samples** and need no sample rate; the bare ones take
`(sample-rate seconds ...)` and round `seconds * sample-rate` to the nearest
sample. Delays are clamped to `[1, 16777216]` samples.

| Constructor | Produces |
|---|---|
| `delay-n n` / `delay sr secs` | the input, `n` samples late (the first `n` outputs are 0) |
| `comb-ff-n n g` / `comb-ff sr secs g` | feedforward comb: `y = x + g*x[n-D]` (always stable) |
| `comb-n n g` / `comb sr secs g` | feedback comb: `y = x + g*y[n-D]` -- an echo train; needs `\|g\| < 1` |
| `allpass-n n g` / `allpass sr secs g` | Schroeder allpass: unit gain, smeared phase; needs `\|g\| < 1` |

Feedback gains are not clamped: `|g| >= 1` grows without bound. A unit
impulse shows each response:

```turmeric
(defmodule demo
  (import signal/core  :refer [sample])
  (import signal/delay :refer [delay-n comb allpass-n])

  ;; A unit impulse: 1.0 at t = 0, else 0.0. Sampled here at t = 0, 1, 2, ...
  (defn impulse [t : float] : float
    (if (< t 0.5) 1.0 0.0))

  (defn main [] : int
    (let [d ((delay-n 2) impulse)]
      (println (sample d 0.0))     ;; 0
      (println (sample d 1.0))     ;; 0
      (println (sample d 2.0)))    ;; 1 (the impulse, two samples late)
    ;; 100 Hz * 0.02 s = 2 samples, feedback 0.5: echoes 1, 0.5, 0.25 ...
    (let [c ((comb 100.0 0.02 0.5) impulse)]
      (println (sample c 0.0))     ;; 1
      (println (sample c 1.0))     ;; 0
      (println (sample c 2.0))     ;; 0.5
      (println (sample c 3.0))     ;; 0
      (println (sample c 4.0)))    ;; 0.25
    (let [a ((allpass-n 2 0.5) impulse)]
      (println (sample a 0.0))     ;; -0.5
      (println (sample a 1.0))     ;; 0
      (println (sample a 2.0)))    ;; 0.75
    0))
```

```sweet-exp
defmodule demo
  import signal/core  :refer [sample]
  import signal/delay :refer [delay-n comb allpass-n]

  ;; A unit impulse: 1.0 at t = 0, else 0.0. Sampled here at t = 0, 1, 2, ...
  defn impulse [t : float] : float
    if {t < 0.5} 1.0 0.0

  defn main [] : int
    let [d (delay-n(2) impulse)]
      println $ sample d 0.0      ;; 0
      println $ sample d 1.0      ;; 0
      println $ sample d 2.0      ;; 1 (the impulse, two samples late)
    ;; 100 Hz * 0.02 s = 2 samples, feedback 0.5: echoes 1, 0.5, 0.25 ...
    let [c (comb(100.0 0.02 0.5) impulse)]
      println $ sample c 0.0      ;; 1
      println $ sample c 1.0      ;; 0
      println $ sample c 2.0      ;; 0.5
      println $ sample c 3.0      ;; 0
      println $ sample c 4.0      ;; 0.25
    let [a (allpass-n(2 0.5) impulse)]
      println $ sample a 0.0      ;; -0.5
      println $ sample a 1.0      ;; 0
      println $ sample a 2.0      ;; 0.75
    0
```

These are the parts of a Schroeder reverb: several feedback `comb`s with
unrelated delays (around 30 to 45 ms) in parallel, summed, then two or three
short `allpass`es in series to diffuse the echoes.

---

## 7. Noise

Generators, like the oscillators: apply the SF to any signal (it is ignored).
Output is always within `[-1, 1]`.

| Constructor | Spectrum | Method |
|---|---|---|
| `white seed` | flat | SplitMix64, uniform in `[-1, 1)` |
| `pink seed` | -3 dB/oct (1/f) | Kellet's 7-pole filter on white |
| `brown seed` | -6 dB/oct | leaky integral of white |
| `blue seed` | +3 dB/oct | first difference of pink |
| `violet seed` | +6 dB/oct | first difference of white |

The seeded constructors keep their generator state in a private cell, so the
signal's effect row is `#fx{}` and a seed always yields the same samples. The
`*-from` constructors (`white-from`, `pink-from`, ...) instead take a source
`(fn [] #fx{e} float)` of raw white samples and return a signal whose row is
`#fx{e}`: pass `(rng seed)` and it stays pure; pass an entropy read or a live
input and it is honestly `#fx{IO}`, and the compiler rejects using it from a
function declared `#fx{}` (`TUR-E0009`).

```turmeric
(defmodule demo
  (import signal/core  :refer [constant sample])
  (import signal/noise :refer [rng white pink white-from])

  (defn main [] : int
    (let [z  (constant 0.0)
          w1 ((white 42) z)
          w2 ((white 42) z)
          p  ((pink 42) z)
          w3 ((white-from (rng 42)) z)]
      (println (sample w1 0.0))    ;; 0.48313
      (println (sample w2 0.0))    ;; 0.48313 (same seed, same stream)
      (println (sample p 0.0))     ;; 0.0875385 (the same stream, colored)
      (println (sample w3 0.0)))   ;; 0.48313 (white-from passes it through)
    0))
```

```sweet-exp
defmodule demo
  import signal/core  :refer [constant sample]
  import signal/noise :refer [rng white pink white-from]

  defn main [] : int
    let [z  constant(0.0)
         w1 (white(42) z)
         w2 (white(42) z)
         p  (pink(42) z)
         w3 (white-from(rng(42)) z)]
      println $ sample w1 0.0     ;; 0.48313
      println $ sample w2 0.0     ;; 0.48313 (same seed, same stream)
      println $ sample p 0.0      ;; 0.0875385 (the same stream, colored)
      println $ sample w3 0.0     ;; 0.48313 (white-from passes it through)
    0
```

Noise through a filter is the raw material of most percussion: high-passed
`white` under a short envelope is a hi-hat, band-passed it is a snare's
rattle, and a slowly swept `svf-bp` over `pink` is wind.

---

## 8. Modulation

Modulation is one signal steering a parameter of another. Four kinds cover
most of synthesis, and each is a few lines here.

### Ring modulation

Multiply two signals. Since `sin(a)*sin(b) = (cos(a-b) - cos(a+b)) / 2`,
ring-modulating a 440 Hz carrier with 100 Hz yields two new tones at 340 Hz
and 540 Hz -- and neither of the originals. With non-harmonic ratios that is
the metallic, bell-and-robot sound. Lift the modulator to `0.5 +/- 0.5` first
and the carrier survives between the sidebands: that is amplitude modulation,
and at a few Hz it is tremolo.

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample pair-signals])
  (import signal/osc    :refer [sine])
  (import signal/shaper :refer [gain offset multiply add])

  (defn main [] : int
    (let [z         (constant 0.0)
          carrier   ((sine 440.0 0.0) z)
          modulator ((sine 100.0 0.0) z)
          ml        (multiply)
          ring      (ml (pair-signals carrier modulator))
          ;; the same thing written as its two sidebands
          quarter   1.5707963267948966
          lower     ((gain 0.5) ((sine 340.0 quarter) z))
          upper     ((gain -0.5) ((sine 540.0 quarter) z))
          ad        (add)
          sides     (ad (pair-signals lower upper))
          ;; AM: the modulator scaled to 0..1
          depth     ((offset 0.5) ((gain 0.5) modulator))
          ml2       (multiply)
          am        (ml2 (pair-signals carrier depth))]
      (println (sample ring 0.0123))   ;; 0.521033
      (println (sample sides 0.0123))  ;; 0.521033 (340 Hz and 540 Hz)
      (println (sample am 0.0025))     ;; 0.587785 (modulator at its peak: all carrier)
      (println (sample am 0.0075)))    ;; 0 (modulator at its trough: silence)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample pair-signals]
  import signal/osc    :refer [sine]
  import signal/shaper :refer [gain offset multiply add]

  defn main [] : int
    let [z         constant(0.0)
         carrier   (sine(440.0 0.0) z)
         modulator (sine(100.0 0.0) z)
         ml        multiply()
         ring      ml(pair-signals(carrier modulator))
         ;; the same thing written as its two sidebands
         quarter   1.5707963267948966
         lower     (gain(0.5) (sine(340.0 quarter) z))
         upper     (gain(-0.5) (sine(540.0 quarter) z))
         ad        add()
         sides     ad(pair-signals(lower upper))
         ;; AM: the modulator scaled to 0..1
         depth     (offset(0.5) (gain(0.5) modulator))
         ml2       multiply()
         am        ml2(pair-signals(carrier depth))]
      println $ sample ring 0.0123    ;; 0.521033
      println $ sample sides 0.0123   ;; 0.521033 (340 Hz and 540 Hz)
      println $ sample am 0.0025      ;; 0.587785 (modulator at its peak: all carrier)
      println $ sample am 0.0075      ;; 0 (modulator at its trough: silence)
    0
```

Ring and AM are products of pure signals, so they are pure. If either input
carries a DC offset, follow the product with `dc-block`.

### Phase modulation (PM)

`(pm-sine freq)` is an SF whose **input is a phase offset in radians**: it
emits `sin(2*pi*freq*t + m(t))`. With a sine modulator, the modulator's peak
is the *modulation index*, and the spectrum is the carrier plus sidebands at
`carrier +/- n * modulator` Hz whose strengths follow the Bessel functions of
the index -- a higher index, a brighter sound. This is what most digital "FM"
synthesizers (the Yamaha DX7 included) actually do.

The carrier:modulator ratio picks the timbre. Integer ratios (1:1, 1:2, 2:1)
keep the sidebands on a harmonic series; others (1:1.4, 1:3.5) put them off
it, which is how FM makes bells and gongs. And an envelope on the index makes
the tone start bright and mellow as it decays:

```turmeric
(defmodule demo
  (import signal/core     :refer [constant sample pair-signals])
  (import signal/osc      :refer [sine pm-sine])
  (import signal/shaper   :refer [gain multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (load "stdlib/math.tur")   ;; sin, for the hand-computed reference

  (defn main [] : int
    ;; A 110 Hz modulator scaled to 2 radians: index 2.
    (let [z     (constant 0.0)
          index ((gain 2.0) ((sine 110.0 0.0) z))
          pm    ((pm-sine 440.0) index)
          t     0.0123
          pi    3.141592653589793
          phase (+ (* 2.0 pi 440.0 t) (* 2.0 (sin (* 2.0 pi 110.0 t))))]
      (println (sample pm t))       ;; -0.863752
      (println (sin phase)))        ;; -0.863752 (by hand)
    ;; A bell: ratio 1:1.4, index enveloped from 6 down to 0 over 1.5 s.
    (let [z     (constant 0.0)
          decay ((adsr-fixed (make-struct ADSRParams 0.001 1.5 0.0 0.1) 1.6) z)
          depth ((gain 6.0) decay)
          ml    (multiply)
          index (ml (pair-signals depth ((sine 280.0 0.0) z)))
          bell  ((pm-sine 200.0) index)]
      (println (sample depth 0.001))    ;; 6 (the index at the end of the attack)
      (println (sample depth 0.751))    ;; 3 (half way down the decay)
      (println (sample bell 0.0123)))   ;; -0.973631
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant sample pair-signals]
  import signal/osc      :refer [sine pm-sine]
  import signal/shaper   :refer [gain multiply]
  import signal/envelope :refer [ADSRParams adsr-fixed]
  load "stdlib/math.tur"   ;; sin, for the hand-computed reference

  defn main [] : int
    ;; A 110 Hz modulator scaled to 2 radians: index 2.
    let [z     constant(0.0)
         index (gain(2.0) (sine(110.0 0.0) z))
         pm    (pm-sine(440.0) index)
         t     0.0123
         pi    3.141592653589793
         phase {{2.0 * pi * 440.0 * t} + {2.0 * sin({2.0 * pi * 110.0 * t})}}]
      println $ sample pm t        ;; -0.863752
      println $ sin phase          ;; -0.863752 (by hand)
    ;; A bell: ratio 1:1.4, index enveloped from 6 down to 0 over 1.5 s.
    let [z     constant(0.0)
         decay (adsr-fixed(make-struct(ADSRParams 0.001 1.5 0.0 0.1) 1.6) z)
         depth (gain(6.0) decay)
         ml    multiply()
         index ml(pair-signals(depth (sine(280.0 0.0) z)))
         bell  (pm-sine(200.0) index)]
      println $ sample depth 0.001     ;; 6 (the index at the end of the attack)
      println $ sample depth 0.751     ;; 3 (half way down the decay)
      println $ sample bell 0.0123     ;; -0.973631
    0
```

`pm-sine` is pure: it computes its phase from `t`, so it can be sampled in any
order, and its modulator may be any signal -- including another `pm-sine`, for
DX-style stacks of operators.

### Frequency modulation (FM)

`(fm-sine sample-rate freq)` is an SF whose **input is a frequency deviation
in Hz**: the instantaneous frequency is `freq + m(t)`, and the phase is its
running integral (trapezoidal, one step per sample). So a modulator that peaks
at 220 Hz swings the pitch 220 Hz either way, and a constant modulator simply
retunes the carrier -- a negative total frequency runs the phase backwards
(through-zero FM). Because it integrates, `fm-sine` is **stateful**: sample it
once per sample, in order, on its sample-rate grid.

FM and PM are two views of one thing. The phase that FM integrates up from
`D*cos(2*pi*fm*t)` Hz is `(D/fm)*sin(2*pi*fm*t)` radians, so FM by a cosine of
peak D is PM by a sine of index `D/fm`:

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample])
  (import signal/osc    :refer [sine fm-sine pm-sine])
  (import signal/shaper :refer [gain])

  ;; walk-to -- walk `y` through n = 0, 1, ..., k on a 48 kHz grid and return
  ;; sample k. A stateful signal has to be walked up to the time you want.
  (defn walk-to [^fat y : (fn [float] float) k : int] : float
    (let [^mut n 0
          ^mut v 0.0]
      (while (<= n k)
        (set! v (y (/ (as float n) 48000.0)))
        (set! n (+ n 1)))
      v))

  (defn main [] : int
    (let [z     (constant 0.0)
          ;; 440 Hz, swung +/- 220 Hz by a 110 Hz cosine ...
          swing ((gain 220.0) ((sine 110.0 1.5707963267948966) z))
          fm    ((fm-sine 48000.0 440.0) swing)
          ;; ... is 440 Hz phase-modulated with index 220 / 110 = 2.
          pm    ((pm-sine 440.0) ((gain 2.0) ((sine 110.0 0.0) z)))
          ;; Slow, shallow FM is vibrato: +/- 6 Hz, five times a second.
          vib   ((fm-sine 48000.0 440.0) ((gain 6.0) ((sine 5.0 0.0) z)))]
      (println (walk-to fm 590))                   ;; -0.855503
      (println (sample pm (/ 590.0 48000.0)))    ;; -0.855517 (the same, to 1e-5)
      (println (walk-to vib 24000)))               ;; 0.675463 = sin(2.4)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample]
  import signal/osc    :refer [sine fm-sine pm-sine]
  import signal/shaper :refer [gain]

  ;; walk-to -- walk `y` through n = 0, 1, ..., k on a 48 kHz grid and return
  ;; sample k. A stateful signal has to be walked up to the time you want.
  defn walk-to [^fat y : (fn [float] float) k : int] : float
    let [^mut n 0
         ^mut v 0.0]
      while {n <= k}
        set! v y({as(float n) / 48000.0})
        set! n {n + 1}
      v

  defn main [] : int
    let [z     constant(0.0)
         ;; 440 Hz, swung +/- 220 Hz by a 110 Hz cosine ...
         swing (gain(220.0) (sine(110.0 1.5707963267948966) z))
         fm    (fm-sine(48000.0 440.0) swing)
         ;; ... is 440 Hz phase-modulated with index 220 / 110 = 2.
         pm    (pm-sine(440.0) (gain(2.0) (sine(110.0 0.0) z)))
         ;; Slow, shallow FM is vibrato: +/- 6 Hz, five times a second.
         vib   (fm-sine(48000.0 440.0) (gain(6.0) (sine(5.0 0.0) z)))]
      println $ walk-to fm 590                     ;; -0.855503
      println $ sample pm {590.0 / 48000.0}      ;; -0.855517 (the same, to 1e-5)
      println $ walk-to vib 24000                  ;; 0.675463 = sin(2.4)
    0
```

The vibrato line shows what integration means: half a second in, the 6 Hz
swing has pushed the phase `(6/5) * (1 - cos(5*pi)) = 2.4` radians ahead of a
plain 440 Hz sine, and it stays there until the swing brings it back.

Which to use? The sounds are interchangeable for a sine modulator; the
parameters differ. **PM** keeps the index -- and so the timbre -- fixed when
you change the modulator's pitch, it is pure, and a DC offset in the modulator
does nothing audible. **FM** is the one to use when the modulator is a pitch
(vibrato, glide, pitch envelopes, through-zero effects), because what you
specify is a deviation in Hz.

---

## 9. Buffers and buffer scanning

`signal/buffer` holds sample buffers. A `Buffer` is a fixed-length array of
samples, shared by handle. Fill it by rendering a signal into it, or by
writing samples:

| Function | Does |
|---|---|
| `buffer-render sr n sig` | a new buffer of `n` samples, sample `k` = `sig` at `t = k / sr` (sampled in order, so stateful signals render correctly) |
| `buffer-new n` | `n` silent samples |
| `buffer-len buf` | the length |
| `buffer-get buf i` / `buffer-set! buf i x` | one sample; the index wraps, so `-1` is the last |
| `buffer-read buf pos` | linearly interpolated read at `pos` buffer lengths (wraps) |
| `scan buf` | an SF: **input = read position**, output = the buffer there |

`scan` is where buffers meet signals. Its input signal is the read position in
*buffer lengths* -- 0 is the start, 0.5 the middle, and it wraps, so 1 is the
start again and -0.25 is three quarters in. Whatever drives the position
decides how the buffer plays, and `sawtooth`, a ramp in `[0, 1)`, is the
natural driver:

- A buffer holding **one cycle** of a waveform, scanned by a `sawtooth` at
  f Hz, is an f Hz oscillator: a **wavetable** oscillator. The table can hold
  any shape you can render -- here a fundamental plus a third harmonic.
- A **recording**, scanned by a ramp that crosses it once per its duration,
  plays back at the original speed, looped. Scan faster and it plays faster
  *and higher*, like tape: 1.5 times the speed is a fifth up.
- A position that runs backwards plays the buffer **in reverse**, and one that
  rises and falls **scrubs** back and forth over it.

```turmeric
(defmodule demo
  (import signal/core   :refer [constant sample pair-signals map-signal])
  (import signal/osc    :refer [sine sawtooth triangle])
  (import signal/shaper :refer [gain add])
  (import signal/buffer :refer [buffer-render buffer-len scan])

  (defn main [] : int
    ;; A wavetable: one cycle of a 1 Hz shape, 2048 points, scanned at 220 Hz.
    (let [z     (constant 0.0)
          ad    (add)
          shape (ad (pair-signals ((sine 1.0 0.0) z)
                                  ((gain 0.3) ((sine 3.0 0.0) z))))
          table (buffer-render 2048.0 2048 shape)
          osc   ((scan table) ((sawtooth 220.0) z))]
      (println (buffer-len table))               ;; 2048
      (println (sample osc 0.001))               ;; 0.72899
      (println (sample shape (* 220.0 0.001))))  ;; 0.728989 (the shape, 220x faster)
    ;; Half a second of a 440 Hz tone at 48 kHz, played four ways.
    (let [z        (constant 0.0)
          tape     (buffer-render 48000.0 24000 ((sine 440.0 0.0) z))
          normal   ((scan tape) ((sawtooth 2.0) z))   ;; once per 0.5 s
          fifth    ((scan tape) ((sawtooth 3.0) z))   ;; 1.5x faster
          backward ((scan tape) ((gain -1.0) ((sawtooth 2.0) z)))
          ;; a triangle is -1..1; map it to 0..0.5 of the tape and back
          rocking  (map-signal (fn [x : float] : float (* 0.25 (+ x 1.0)))
                               ((triangle 0.5) z))
          scrub    ((scan tape) rocking)]
      (println (sample normal 0.01))                 ;; 0.587785
      (println (sample ((sine 440.0 0.0) z) 0.01))   ;; 0.587785 (the original)
      (println (sample fifth 0.01))                  ;; -0.587785
      (println (sample ((sine 660.0 0.0) z) 0.01))   ;; -0.587785 (660 Hz: a fifth up)
      (println (sample backward 0.01))               ;; -0.587785 (the tape at 0.49 s)
      (println (sample rocking 1.0))                 ;; 0.5 (half way in at t = 1 ...)
      (println (sample rocking 2.0)))                ;; 0 (... and back at t = 2)
    0))
```

```sweet-exp
defmodule demo
  import signal/core   :refer [constant sample pair-signals map-signal]
  import signal/osc    :refer [sine sawtooth triangle]
  import signal/shaper :refer [gain add]
  import signal/buffer :refer [buffer-render buffer-len scan]

  defn main [] : int
    ;; A wavetable: one cycle of a 1 Hz shape, 2048 points, scanned at 220 Hz.
    let [z     constant(0.0)
         ad    add()
         shape ad(pair-signals((sine(1.0 0.0) z)
                               (gain(0.3) (sine(3.0 0.0) z))))
         table buffer-render(2048.0 2048 shape)
         osc   (scan(table) (sawtooth(220.0) z))]
      println $ buffer-len table                ;; 2048
      println $ sample osc 0.001                ;; 0.72899
      println $ sample shape {220.0 * 0.001}    ;; 0.728989 (the shape, 220x faster)
    ;; Half a second of a 440 Hz tone at 48 kHz, played four ways.
    let [z        constant(0.0)
         tape     buffer-render(48000.0 24000 (sine(440.0 0.0) z))
         normal   (scan(tape) (sawtooth(2.0) z))   ;; once per 0.5 s
         fifth    (scan(tape) (sawtooth(3.0) z))   ;; 1.5x faster
         backward (scan(tape) (gain(-1.0) (sawtooth(2.0) z)))
         ;; a triangle is -1..1; map it to 0..0.5 of the tape and back
         rocking  map-signal((fn [x : float] : float {0.25 * {x + 1.0}})
                             (triangle(0.5) z))
         scrub    (scan(tape) rocking)]
      println $ sample normal 0.01                  ;; 0.587785
      println $ sample (sine(440.0 0.0) z) 0.01     ;; 0.587785 (the original)
      println $ sample fifth 0.01                   ;; -0.587785
      println $ sample (sine(660.0 0.0) z) 0.01     ;; -0.587785 (660 Hz: a fifth up)
      println $ sample backward 0.01                ;; -0.587785 (the tape at 0.49 s)
      println $ sample rocking 1.0                  ;; 0.5 (half way in at t = 1 ...)
      println $ sample rocking 2.0                  ;; 0 (... and back at t = 2)
    0
```

`scan` keeps no state, so a scan driven by an oscillator is pure. Rendering is
also how to *freeze* a stateful signal: `(buffer-render 48000.0 n ((white 1) z))`
is noise you can sample in any order, loop, or read twice.

Two cautions. A table scanned at a high pitch aliases just as a naive
oscillator does whenever it holds harmonics above Nyquist divided by the
pitch; render bright tables for low notes, or several tables band-limited per
octave. And linear interpolation is exact only on the sample grid: off it, a
2048-point sine table is good to about 1e-6, a 440 Hz recording at 48 kHz to
about 4e-4.

---

## 10. Pitches, melodies and cycles

### Note numbers and Hz: `mtof` and `ftom`

`signal/pitch` converts between MIDI note numbers and frequencies, in
twelve-tone equal temperament with A4 = 440 Hz = note 69, so middle C (C4) is
note 60. `mtof` goes from note to Hz, `ftom` back. Note numbers are floats:
each 1.0 is a semitone, each 12.0 an octave, and 60.5 is a quarter tone above
middle C. Both are plain `float -> float` functions; `map-signal` lifts one
onto a signal, so a pitch contour in notes becomes a frequency signal:

```turmeric
(defmodule demo
  (import signal/core  :refer [sample map-signal])
  (import signal/pitch :refer [mtof ftom])

  (defn main [] : int
    (println (mtof 69.0))       ;; 440 (A4)
    (println (mtof 60.0))       ;; 261.626 (middle C)
    (println (mtof 72.0))       ;; 523.251 (an octave up: twice the Hz)
    (println (ftom 450.0))      ;; 69.3891 (A4, 39 cents sharp)
    ;; A glide up one whole tone a second, from A4.
    (let [glide (fn [t : float] : float (+ 69.0 (* 2.0 t)))
          hz    (map-signal mtof glide)]
      (println (sample hz 0.5)))  ;; 466.164 (B-flat 4, half way)
    0))
```

```sweet-exp
defmodule demo
  import signal/core  :refer [sample map-signal]
  import signal/pitch :refer [mtof ftom]

  defn main [] : int
    println $ mtof 69.0         ;; 440 (A4)
    println $ mtof 60.0         ;; 261.626 (middle C)
    println $ mtof 72.0         ;; 523.251 (an octave up: twice the Hz)
    println $ ftom 450.0        ;; 69.3891 (A4, 39 cents sharp)
    ;; A glide up one whole tone a second, from A4.
    let [glide (fn [t : float] : float {69.0 + {2.0 * t}})
         hz    map-signal(mtof glide)]
      println $ sample hz 0.5   ;; 466.164 (B-flat 4, half way)
    0
```

`ftom` of a frequency that is not a tempered pitch is fractional: round it
for the nearest note, and the rest is the offset in semitones (times 100 for
cents). The frequency must be positive.

### Playing a list of pitches in order

A melody is a list of note numbers and a step length. The note sounding at
time `t` is the one at index `t / step`, so the pitch is a pure function of
time and needs no state of its own. Two details make it sound right:

- **The oscillator is `fm-sine` with a carrier of 0**, so its input signal is
  its whole frequency. It integrates that frequency, so the phase runs on
  smoothly from one note into the next; switching between separate `sine`s
  would jump the phase at every note and click.
- **The envelope restarts every step.** The ADSR is a function of time, so
  the per-note envelope is the ADSR sampled at the time since the step began.

`heard` checks the result: it counts the rendered audio's zero crossings
while each note is held and converts the count back to a note with `ftom`.

```turmeric
(defmodule demo
  (import signal/core     :refer [constant sample pair-signals])
  (import signal/osc      :refer [fm-sine])
  (import signal/pitch    :refer [mtof ftom])
  (import signal/shaper   :refer [multiply])
  (import signal/envelope :refer [ADSRParams adsr-fixed])
  (import signal/buffer   :refer [Buffer buffer-render buffer-get])

  ;; note-at -- the note sounding at time t: one note per `step` seconds,
  ;; in order, starting over after the last.
  (defn note-at [notes : (Vec float) step : float t : float] : float
    (vec-get notes (mod (as int (/ t step)) (vec-len notes))))

  ;; heard -- the nearest note to what `b` plays in [from, from + n) at
  ;; 48 kHz: upward zero crossings per second, back to a note number.
  (defn heard [b : Buffer from : int n : int] : int
    (let [^mut i  (+ from 1)
          ^mut up 0]
      (while (< i (+ from n))
        (when (and (< (buffer-get b (- i 1)) 0.0) (>= (buffer-get b i) 0.0))
          (set! up (+ up 1)))
        (set! i (+ i 1)))
      (as int (+ 0.5 (ftom (/ (as float up) (/ (as float n) 48000.0)))))))

  (defn main [] : int
    (let [melody [60.0 64.0 67.0 72.0]   ;; C4 E4 G4 C5
          step   0.25                     ;; seconds per note
          z      (constant 0.0)
          freq   (fn [t : float] : float (mtof (note-at melody step t)))
          tone   ((fm-sine 48000.0 0.0) freq)
          env    ((adsr-fixed (make-struct ADSRParams 0.005 0.05 0.7 0.04) 0.2) z)
          per    (fn [t : float] : float
                   (sample env (- t (* step (as float (as int (/ t step)))))))
          ml     (multiply)
          voice  (ml (pair-signals tone per))
          out    (buffer-render 48000.0 48000 voice)]   ;; one pass: 1 s
      (println (heard out 0 9600))       ;; 60
      (println (heard out 12000 9600))   ;; 64
      (println (heard out 24000 9600))   ;; 67
      (println (heard out 36000 9600)))  ;; 72
    0))
```

```sweet-exp
defmodule demo
  import signal/core     :refer [constant sample pair-signals]
  import signal/osc      :refer [fm-sine]
  import signal/pitch    :refer [mtof ftom]
  import signal/shaper   :refer [multiply]
  import signal/envelope :refer [ADSRParams adsr-fixed]
  import signal/buffer   :refer [Buffer buffer-render buffer-get]

  ;; note-at -- the note sounding at time t: one note per `step` seconds,
  ;; in order, starting over after the last.
  defn note-at [notes : (Vec float) step : float t : float] : float
    vec-get notes mod(as(int {t / step}) vec-len(notes))

  ;; heard -- the nearest note to what `b` plays in [from, from + n) at
  ;; 48 kHz: upward zero crossings per second, back to a note number.
  defn heard [b : Buffer from : int n : int] : int
    let [^mut i  {from + 1}
         ^mut up 0]
      while {i < {from + n}}
        when (and {buffer-get(b {i - 1}) < 0.0} {buffer-get(b i) >= 0.0})
          set! up {up + 1}
        set! i {i + 1}
      as int {0.5 + ftom({as(float up) / {as(float n) / 48000.0}})}

  defn main [] : int
    let [melody [60.0 64.0 67.0 72.0]   ;; C4 E4 G4 C5
         step   0.25                     ;; seconds per note
         z      constant(0.0)
         freq   (fn [t : float] : float mtof(note-at(melody step t)))
         tone   (fm-sine(48000.0 0.0) freq)
         env    (adsr-fixed(make-struct(ADSRParams 0.005 0.05 0.7 0.04) 0.2) z)
         per    (fn [t : float] : float
                  sample(env {t - {step * as(float as(int {t / step}))}}))
         ml     multiply()
         voice  ml(pair-signals(tone per))
         out    buffer-render(48000.0 48000 voice)]   ;; one pass: 1 s
      println $ heard out 0 9600        ;; 60
      println $ heard out 12000 9600    ;; 64
      println $ heard out 24000 9600    ;; 67
      println $ heard out 36000 9600    ;; 72
    0
```

Change `melody` and `step` to play something else; past the last note it
starts over. To play it once, clamp the index instead of wrapping it. Swap
`fm-sine` for a band-limited sawtooth and the pitch still has to reach it as a
frequency signal, which is what `fm-sine`'s input is for.

### A `cycle`: the next value, each time it is asked

`note-at` answers "what is playing at time `t`?". Event-driven code asks a
different question -- "what comes next?" -- every time a note starts, a step
fires or a key is pressed. The natural shape for that is a **closure that
owns its position**: `cycle` takes a list and returns a function of no
arguments that hands out the values in order, one per call, starting over
after the last. It is the same pattern as `signal/noise`'s `rng`: the state
lives in a mutable only the closure can reach, so each `cycle` call makes an
independent source.

```turmeric
(defmodule demo
  (import signal/pitch :refer [mtof])

  ;; cycle -- hand out `values` in order, one per call, wrapping around.
  (defn cycle [values : (Vec float)] : (fn [] float)
    (let [^mut i 0]
      (fn [] : float
        (let [v (vec-get values (mod i (vec-len values)))]
          (set! i (+ i 1))
          v))))

  (defn main [] : int
    (let [next-note (cycle [60.0 64.0 67.0])
          next-amp  (cycle [1.0 0.5 0.75 0.5])
          other     (cycle [60.0 64.0 67.0])]
      ;; Three notes against four accents: the pairing repeats every 12.
      (println (next-note))      ;; 60
      (println (next-amp))       ;; 1
      (println (next-note))      ;; 64
      (println (next-amp))       ;; 0.5
      (println (next-note))      ;; 67
      (println (next-amp))       ;; 0.75
      (println (next-note))      ;; 60
      (println (next-amp))       ;; 0.5
      (println (mtof (next-note)))   ;; 329.628 (E4)
      (println (other)))             ;; 60 (its own position)
    0))
```

```sweet-exp
defmodule demo
  import signal/pitch :refer [mtof]

  ;; cycle -- hand out `values` in order, one per call, wrapping around.
  defn cycle [values : (Vec float)] : (fn [] float)
    let [^mut i 0]
      (fn [] : float
        (let [v vec-get(values mod(i vec-len(values)))]
          set!(i {i + 1})
          v))

  defn main [] : int
    let [next-note cycle([60.0 64.0 67.0])
         next-amp  cycle([1.0 0.5 0.75 0.5])
         other     cycle([60.0 64.0 67.0])]
      ;; Three notes against four accents: the pairing repeats every 12.
      println $ next-note()     ;; 60
      println $ next-amp()      ;; 1
      println $ next-note()     ;; 64
      println $ next-amp()      ;; 0.5
      println $ next-note()     ;; 67
      println $ next-amp()      ;; 0.75
      println $ next-note()     ;; 60
      println $ next-amp()      ;; 0.5
      println $ mtof next-note()  ;; 329.628 (E4)
      println $ other()           ;; 60 (its own position)
    0
```

A cycle is stateful in the same way a filter or a noise generator is: every
call advances it. Do not sample it from inside a signal -- a signal is asked
for a value 48,000 times a second, and would race through the list -- and do
not share one cycle between two consumers that each expect the whole
sequence; give each its own. When the question really is "what is playing at
time `t`", use the pure `note-at` shape above instead.

---

## Stateful signals and sample order

`low-pass`, `high-pass` and every other filter, every delay effect, every noise
generator and `fm-sine` advance their internal state once per `sample` call
and ignore `t`. Sample each instance **exactly once per sample period, in
order**, starting at `t = 0`. Feeding one instance to two consumers (for
example both halves of `pair-signals`) advances it twice, so each consumer sees
only part of the stream and a delay's effective length changes. Build one
instance per consumer -- applying the SF again is cheap -- or render the
signal into a buffer and scan that. The oscillators (other than `fm-sine`),
shapers, envelopes and `scan` are pure functions of `t` and have no such
restriction.

## Calling conventions worth knowing

- **Captureless SFs** (`invert`, `abs-sf`, `add`, `multiply`) take no
  parameters. Bind the SF to a name first, then apply it -- applying the bare
  `(invert)` result inline does not compile.
- **`effects-chain`** returns the chained signal as a raw pointer. Bind the
  result as a `^fat` signal before sampling it. (`map-signal` returns an
  ordinary signal; sample it directly.)
- **`ADSRParams`** is a `:copy` struct; build it with `make-struct` and pass
  it by value to `adsr-fixed` / `adsr-gen`.
- **Sample rates come first** in every constructor that takes one, except the
  `hf-osc` oscillators, which take it last: `(hf-sawtooth freq sample-rate)`.

---

## When not to use this

- **You need sound out of a speaker.** This spice computes samples; it has no
  audio device or file output. Render with `sample` (or `buffer-render`) and
  hand the numbers to whatever plays them.
- **You need Karplus-Strong, granular synthesis, or band-limited wavetable
  sets.** None ships. A feedback comb has no filter in its loop, so it is not
  a plucked string; a single scanned table aliases at high pitches.
- **You need analog-modeled saturation.** The ladder is the linear model and
  nothing in `signal/filter` saturates; put `saturate-tanh` before or after a
  filter, not inside it.
- **You need a poly-synth, a full step sequencer, or a voice manager.**
  Section 10 shows the building blocks -- a time-indexed melody and a
  `cycle` -- but scheduling, polyphony and voice allocation are composition
  layers above this spice.
- **You need throughput.** Every sample of every stage is a closure call, which
  is fine for offline rendering and tests, and slow for many real-time voices.

---

## Examples

Ten runnable examples live under `examples/`:

```sh
cd spices/signal
tur run examples/01_constant_and_time.tur       # constant + time-signal
tur run examples/02_oscillators.tur             # sine/square/sawtooth/triangle at audible pitches
tur run examples/03_filters_and_shapers.tur     # one-pole filters + scalar shapers
tur run examples/04_envelopes.tur               # ADSR
tur run examples/05_simple_voice.tur            # an A3 voice: oscillator, chain, envelope
tur run examples/06_hf_oscillators.tur          # naive vs band-limited sawtooth at 7.9 kHz
tur run examples/07_delay_and_noise.tur         # delay, comb, allpass, colored noise
tur run examples/08_filters.tur                 # biquads, state-variable sweep, ladder
tur run examples/09_modulation_and_buffers.tur  # ring, AM, PM, FM, wavetable, tape
tur run examples/10_pitch_and_sequences.tur     # mtof/ftom, a melody in order, a cycle
```

Each prints sample values that match the hand-computed references in its
comments.

---

## Tests

```sh
cd spices/signal
tur test tests/signal    # 11 tests, all pass
```

---

## See also

- [API reference](api/)
- [README](../../spices/signal/README.md) -- the full module and symbol tables
- [tur-math guide](math-guide.html) -- scalar helpers (clamp, lerp, remap) used alongside DSP
