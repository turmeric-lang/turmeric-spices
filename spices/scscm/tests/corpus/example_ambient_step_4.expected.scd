SynthDef("pad", { |freq=220, amp=0.15, gate=1| var lfo = 3 * SinOsc.kr(0.1); var osc = SinOsc.ar(freq + lfo, 0); var env = EnvGen.kr(Env.asr(4, 1, 4), gate, doneAction: 2); Out.ar(0, osc * env * amp * 0.5) }).add;
SynthDef("bass", { |freq=110, amp=0.25, gate=1| var osc = SinOsc.ar(freq, 0) * 0.7; var sub = SinOsc.ar(freq * 0.5, 0) * 0.3; var env = EnvGen.kr(Env.perc(0.05, 1.5), gate, doneAction: 2); Out.ar(0, (osc + sub) * env * amp) }).add;
SynthDef("melody", { |freq=440, amp=0.2, gate=1| var osc = SinOsc.ar(freq, 0); var env = EnvGen.kr(Env.perc(0.02, 0.6), gate, doneAction: 2); Out.ar(0, osc * env * amp) }).add;
SynthDef("shimmer", { |freq=880, amp=0.06, gate=1| var osc = SinOsc.ar(freq, 0); var env = EnvGen.kr(Env.perc(0.005, 0.3), gate, doneAction: 2); Out.ar(0, osc * env * amp) }).add;
Synth("pad", [\freq, 220, \amp, 0.15]);
Pbind(\instrument, "bass", \midinote, Pseq.new([36, 38, 40, 38], inf), \dur, 2, \amp, 0.25).play;
Pbind(\instrument, "melody", \midinote, Prand.new([60, 62, 64, 65, 67, 69, 71], inf), \dur, Prand.new([0.5, 1.0, 1.5], inf), \amp, 0.2).play;
Pbind(\instrument, "shimmer", \midinote, Prand.new([84, 86, 88, 90, 91], inf), \dur, Prand.new([0.2, 0.3, 0.4], inf), \amp, 0.06).play;
