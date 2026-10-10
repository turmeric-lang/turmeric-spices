{ SynthDef("pluck", { |freq=440, amp=0.5, decay=0.5| Out.ar(0, { Pluck.ar(WhiteNoise.ar(), Impulse.kr(0), 0.2, 1 / freq, decay, 0.1) * amp }().dup(2)) }).add; pluck = { |...__args| Synth("pluck", __args) } }();
var metro = TempoClock.new(140 / 60);
var melody = [60, 62, 64, 65, 67, 65, 64, 62];
Pbind.new([\instrument, "pluck", \midinote, Pseq.new(melody, 4), \dur, Pseq.new([0.25, 0.25, 0.25, 0.25], inf), \amp, 0.4, \legato, 0.5]);
