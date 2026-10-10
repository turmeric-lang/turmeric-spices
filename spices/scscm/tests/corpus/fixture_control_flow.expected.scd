if (1 == 1) { "yes" } { "no" };
if (5 < 3) { "yes" } { "no" };
if (1 == 1) { if (2 == 2) { "both true" } { "outer true" } } { "outer false" };
if (1 == 1) { "do this" } { nil };
