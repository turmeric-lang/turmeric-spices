{ SynthDef("sine-wave", { |freq=440, amp=0.3, gate=1| Out.ar(0, { SinOsc.ar(freq, 0) * EnvGen.kr(Env.adsr(0.01, 0.1, 0.8, 0.3), gate, (doneAction: 2)) }().dup(2)) }).add; sine_wave = { |...__args| Synth("sine-wave", __args) } }();
{ SynthDef("pluck", { |freq=440, amp=0.5, decay=2| Out.ar(0, { Pluck.ar(WhiteNoise.ar(), Impulse.kr(0), 0.2, 1 / freq, decay, 0.1) * amp }().dup(2)) }).add; pluck = { |...__args| Synth("pluck", __args) } }();
var root = sine-wave(\freq, 60.midicps, \amp, 0.2);
var third = sine-wave(\freq, 64.midicps, \amp, 0.2);
var fifth = sine-wave(\freq, 67.midicps, \amp, 0.2);
TempoClock.default.sched(2, { third.set(\freq, 63.midicps) });
TempoClock.default.sched(4, { { root.free; third.free; fifth.free }() });
