# Changelog

## 0.1.0

Initial release: the shared markup tree and SAX event vocabulary behind
tur-xml and tur-html (turmeric `docs/upcoming/spices/xml-html-parsers-plan.md`).

### Added

- `dom/core`: `Document` / `Node` over an arena-owned tree in vendored C
  (`c/dom/turdom.c`) -- navigation, attributes (qualified or Clark-form
  names), borrowed `cstr` and owned `String` text, `find-all` / `find-first`,
  XML and HTML serialization, structural `dom-equal?`, and `dom-live-bytes`
  for leak checks. Every walk is iterative.
- `ParseError` / `ParseErrorKind` (`Syntax`, `Limit`, `Refused`, `Io`), the
  error both parsers return. `syntax-kind`, `limit-kind`, `refused-kind` and
  `io-kind` return one shared node per kind, so building an error does not
  allocate (and leak) a `:heap` node for its kind.
- `dom/event`: `SaxEvent`, `SaxControl`, `Attr`, `attrs-get`,
  `sax-event-release`, `sax-dispatch`. Events are `:heap` nodes so the SAX
  drivers reclaim them per chunk with a region.
- `dom/ownstr`: a self-contained owned `String` (`:non-null`, so an
  `(Option String)` is one word).
