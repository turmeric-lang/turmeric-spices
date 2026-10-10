SynthDef("sine", { |freq=440, amp=0.1, gate=1| Out.ar(0, SinOsc.ar(freq, 0) * EnvGen.kr(Env.adsr(0.01, 0.1, 0.8, 0.1), gate, doneAction: 2) * amp) }).add;
Synth("sine", [\freq, 440, \amp, 0.1]);
