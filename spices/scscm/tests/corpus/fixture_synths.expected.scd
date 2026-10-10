SynthDef("simple-sine", { |freq, amp| out:ar(sin_osc:ar(freq, amp)) }).add;
SynthDef("filtered-noise", { |amp, cutoff| { |noise| out:ar(lpf:ar(noise, cutoff)) }.value(white_noise:ar(amp)) }).add;
