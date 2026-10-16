# Changelog

## 0.3.0

### Changed

- **BREAKING -- `<%= var %>` HTML-escapes its value.** `&`, `<`, `>`, `"`
  and `'` become `&amp;`, `&lt;`, `&gt;`, `&quot;` and `&#39;`. Before
  0.3.0 the value was written verbatim, so a template that interpolated a
  request value (a URL capture, a form field) into HTML was an XSS hole.
  Templates whose output is not HTML, or that interpolate markup on
  purpose, switch those tags to `<%== var %>`.

### Added

- `<%== var %>` writes the value verbatim. It lexes as token tag 4
  (`RawExprBlock`) and parses to node tag 5 (`RawEmit`). `template/tree`'s
  `Tpl` has no raw variant yet; `tpl-emit` stands for `<%= %>`.

### Fixed

- `tests/smoke.tur` returned 0 whatever its checks said, so `tur test` could
  not fail on it. `main` now returns the number of failed checks.
