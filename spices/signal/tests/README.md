# tur-signal tests

One test module per source module, covering the Tier 1 surface:

| Test | Covers |
|---|---|
| `test_core.tur` | `constant`, `time-signal`, `sample` |
| `test_hf_osc.tur` | `hf-sawtooth`, `hf-square`, `hf-triangle`: naive away from edges, rounded at edges, range, sample-rate 0 |
| `test_noise.tur` | white/pink/brown/blue/violet: determinism, range, spectral tilt order, the `-from` injected forms, effect-row purity |
| `test_osc.tur` | `sine`, `square`, `sawtooth`, `triangle` (zero crossings + peaks) |
| `test_delay.tur` | `delay-n`, `comb-ff-n`, `comb-n`, `allpass-n` impulse responses; the seconds layer; clamping; allpass energy preservation |
| `test_filter.tur` | `low-pass`, `high-pass` step response; per-instance state |
| `test_shaper.tur` | every shaper + the Pair mixers (`mix`, `add`, `multiply`) |
| `test_envelope.tur` | `adsr-fixed` / `adsr-gen` across all ADSR segments |
| `test_compose.tur` | `effects-chain`: empty / single / multi / captureless stages |

Each test's `main` returns the number of failed assertions, so a logic
failure surfaces as a non-zero exit (a `FAIL` line under `tur test`),
not just a printed message.

## Running

```sh
cd spices/signal
tur test tests/signal
# expected: 9 tests, 9 passed, 0 failed
```

Or run a single module directly:

```sh
tur run tests/signal/test_osc.tur
# expected: PASS test_osc
```
