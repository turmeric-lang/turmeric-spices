SynthDef("pad", { |freq=220, amp=0.15, gate=1| var lfo = 3 * SinOsc.kr(0.1); var osc = SinOsc.ar(freq + lfo, 0); var env = EnvGen.kr(Env.asr(4, 1, 4), gate, doneAction: 2); Out.ar(0, osc * env * amp * 0.5) }).add;
SynthDef("bass", { |freq=110, amp=0.25, gate=1| var osc = SinOsc.ar(freq, 0) * 0.7; var sub = SinOsc.ar(freq * 0.5, 0) * 0.3; var env = EnvGen.kr(Env.perc(0.05, 1.5), gate, doneAction: 2); Out.ar(0, (osc + sub) * env * amp) }).add;
Synth("pad", [\freq, 220, \amp, 0.15]);
Pbind(\instrument, "bass", \midinote, Pseq.new([36, 38, 40, 38], inf), \dur, 2, \amp, 0.25).play;
