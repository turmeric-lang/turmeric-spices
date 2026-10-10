SynthDef("pad", { |freq=220, amp=0.15, gate=1| var lfo = 3 * SinOsc.kr(0.1); var osc = SinOsc.ar(freq + lfo, 0); var env = EnvGen.kr(Env.asr(4, 1, 4), gate, (doneAction: 2)); Out.ar(0, *(osc, env, amp, 0.5)) });
Synth("pad", (freq: 220, amp: 0.15));
