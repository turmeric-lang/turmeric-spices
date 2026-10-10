var fm_bell, c_major, freqs;
SynthDef("fm-bell", { |freq=440, amp=0.3, ratio=2.0, index=5.0, decay=2.0| Out.ar(0, { var modulator = SinOsc.ar(freq * ratio, 0) * (freq * index); var carrier = SinOsc.ar(freq + modulator, 0); carrier * EnvGen.kr(Env.perc(0.001, decay), doneAction: 2) * amp }.value.dup(2)) }).add;
fm_bell = { |...scscm_args| Synth("fm-bell", scscm_args) };
c_major = [60, 62, 64, 65, 67, 69, 71, 72];
freqs = c_major.collect({ |note| note.midicps });
[0, 1, 2, 3, 4, 5, 6, 7].do({ |i| TempoClock.default.sched(i * 0.4, { fm_bell.(\freq, freqs.at(i), \decay, 0.5 + (i * 0.3), \index, 8.0 - (i * 0.8)) }) });
