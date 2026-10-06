# tur-test

Testing framework utilities for Turmeric: `deftest`, `assert-eq`, `assert-err`,
`run-tests`.

## Overview

`tur-test` is a Tier 1 spice (pure Turmeric, no C deps). It provides a tiny
test runner suitable for both stdlib-style integration suites and per-spice
unit tests. Tests are declared with `deftest`, assertions live inline, and
`run-tests` prints a summary at the end.

Use it as the default test framework for any spice or application. Because it
is pure Turmeric it works on every platform Turmeric itself supports,
including the WebAssembly build.

## Install

```turmeric no-check
:spices {
  "test" {:url    "https://github.com/turmeric-lang/turmeric-spices"
          :ref    "test-v0.1.0"
          :subdir "spices/test"}
}
```

## Quick start

```turmeric
(import test :refer [deftest assert-eq run-tests])

(deftest "adds correctly"
  (assert-eq (+ 1 2) 3))

(run-tests)
```

```sweet-exp
#lang sweet-exp
import test :refer [deftest assert-eq run-tests]

deftest "adds correctly"
  assert-eq(+(1 2) 3)

run-tests()
```

## See also

- [Running tests guide](https://turmeric-lang.com/docs/html/guides/running-tests-guide.html)
- [API reference](api/)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/test>
