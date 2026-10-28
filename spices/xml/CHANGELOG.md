# Changelog

## Unreleased

### Added

- `xml/markup` (xml-html-followups-plan follow-up A1-A2): `#xml{...}`
  markup literals, the XML counterpart of tur-html's `#html{...}`, checked
  once with expat on first render. `xml*` is the macro behind it.
- `errors/run.sh`: the compile-error table for `#xml{...}` holes.

## 0.2.0

### Added

- `xml/transform` (xml-html-parsers-plan follow-up B4): `sax-transform` and
  `sax-transform-opts` run a document's events through a
  `(fn [SaxEvent] (Cons SaxEvent))` and serialize the result with a dom-core
  `SaxWriter`; `sax-transform-file` streams one file into another without
  holding either. Errors are a `TransformError`: `InputError` (the parse)
  or `OutputError` (the writer's refusal).
- With namespaces off, the identity transform reproduces `dom-serialize`
  byte for byte: `tests/transform.tur`, every xmltest `valid/sa` document in
  `fixtures/conformance.tur`, and a new property of the fuzz pass. With
  namespaces on, the output parses to the same events (tests and fuzz).
- `fixtures/edit-fuzz.tur` fuzzes dom-core's `dom/edit` on XML documents
  (follow-up B2): random edit sequences, refused edits change nothing, and
  the result reparses to an equal tree.

## 0.1.0

Initial release: XML parsing via expat 2.9.0 (turmeric
`docs/upcoming/spices/xml-html-parsers-plan.md`, phases P0-P3 and P7).

### Added

- `xml/sax`: `sax-parse-string` / `sax-parse-file` (and `-opts`), and a push
  parser (`sax-parser-new`, `sax-feed!`, `sax-finish!`) whose events are
  identical to the one-shot parse for any chunking. Text is coalesced;
  memory is constant in the document size.
- `xml/dom`: `dom-parse-string` / `dom-parse-file` (and `-opts`), building the
  dom-core tree from the SAX layer's event queue.
- `ParseOpts` with safe defaults: no external entities or external DTD
  subset (references are `Refused`), expat's amplification limit, depth 256,
  64 MiB of input. With external entities allowed, only local files are read.
- `cmake-deps/expat`: a shim reaching libexpat's CMakeLists in `expat/`.
- Tests for every error class, limits, namespaces, encodings, push parsing,
  the node API and round trips; `fixtures/` runs xmltest from the W3C
  conformance suite (fetched, checksum-pinned) and a mutation fuzz pass.
