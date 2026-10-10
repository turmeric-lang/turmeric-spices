SynthDef("simple-sine", { |freq, amp| out(\ar, sin-osc(\ar, freq, amp)) });
SynthDef("filtered-noise", { |amp, cutoff| { |noise| out(\ar, lpf(\ar, noise, cutoff)) }.value(white-noise(\ar, amp)) });
