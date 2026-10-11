# html5lib tree-construction tests (vendored slice)

These `.dat` files are the tree-construction tests from
[html5lib-tests](https://github.com/html5lib/html5lib-tests), copied unmodified
from commit `9329e64694e7835d0dcff9811e22856ef6ad16f9` (2026-06-22) -- the last
commit before the tree-construction tests moved to web-platform-tests
(`224991e`, "Tree construction tests have moved to WPT"). They are MIT-licensed;
the licence is in `LICENSE` beside them.

`tests/html5lib.tur` runs every test in every file here. Left out of the slice:

- `domjs-unsafe.dat`, `plain-text-unsafe.dat`,
  `pending-spec-changes-plain-text-unsafe.dat` -- they embed raw NUL and other
  control bytes, which a `cstr` cannot carry;
- the `scripted/` directory -- those tests need a script engine.

Skipped inside the files, and counted as skipped: `#script-on` tests (the
parser runs with scripting off), fragment tests whose context element is SVG or
MathML (`html-parse-fragment` takes an HTML tag name), and tests whose data
contains a NUL byte.

Five tests are listed in `tests/html5lib.tur` as known failures: they were
added to html5lib-tests after lexbor 3.0.1 and describe behaviour that release
does not implement (an adoption-agency case around `<marquee>` in
`adoption02.dat`, and the customizable-select `<selectedcontent>` cloning in
`webkit02.dat`). A known failure that starts passing fails the suite, so the
list cannot go stale.
