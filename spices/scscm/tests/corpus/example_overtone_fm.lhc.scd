{ SynthDef("fm-bell", { |freq=440, amp=0.3, ratio=2, index=5, decay=2| Out.ar(0, { var modulator = SinOsc.ar(freq * ratio, 0) * freq * index; var carrier = SinOsc.ar(freq + modulator, 0); *(carrier, EnvGen.kr(Env.perc(0.001, decay), (doneAction: 2)), amp) }().dup(2)) }).add; fm_bell = { |...__args| Synth("fm-bell", __args) } }();
var c_major = [60, 62, 64, 65, 67, 69, 71, 72];
var freqs = c_major.collect({ |note| note.midicps });
[0, 1, 2, 3, 4, 5, 6, 7].do({ |i| TempoClock.default.sched(i * 0.4, { fm-bell(\freq, freqs.at(i), \decay, 0.5 + i * 0.3, \index, 8 - i * 0.8) }) });
