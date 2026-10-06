# tur-regex

PCRE2 regex bindings for Turmeric: compile, match, replace, and named-capture
access.

## Overview

`tur-regex` is a Tier 3 spice (`cmake-dep` -- pulls in `PCRE2 10.44` via
`tur fetch`). It exposes the PCRE2 surface: `regex-compile` produces a
`result<regex>`, `regex-match` returns a capture handle, and the
`regex/capture` module gives positional and named access to matched groups.

Use it for any text processing that goes beyond what `cstr->index-of` or
hand-written scanners cover -- log parsing, tokenization scaffolds, input
validation, etc.

## Install

```turmeric no-check
:spices {
  "regex" {:url    "https://github.com/turmeric-lang/turmeric-spices"
           :ref    "regex-v0.1.0"
           :subdir "spices/regex"}
}
```

## Quick start

```turmeric
(import regex/regex   :refer [regex-compile regex-match regex-free])
(import regex/capture :refer [capture-named])

(let [r (regex-compile "(?P<year>\\d{4})-(?P<month>\\d{2})" 0)]
  (when (ok? r)
    (let [re (ok-val r)
          m  (regex-match re "Today is 2026-05-21")]
      (when (ok? m)
        (println (capture-named (ok-val m) "year"))
        (println (capture-named (ok-val m) "month")))
      (regex-free re))))
```

```sweet-exp
#lang sweet-exp
import regex/regex   :refer [regex-compile regex-match regex-free]
import regex/capture :refer [capture-named]

let [r regex-compile("(?P<year>\\d{4})-(?P<month>\\d{2})" 0)]
  when ok?(r)
    let [re ok-val(r)
         m  regex-match(re "Today is 2026-05-21")]
      when ok?(m)
        println $ capture-named ok-val(m) "year"
        println $ capture-named ok-val(m) "month"
      regex-free(re)
```

## See also

- [Guide](https://turmeric-lang.com/docs/html/guides/regex-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/regex>
