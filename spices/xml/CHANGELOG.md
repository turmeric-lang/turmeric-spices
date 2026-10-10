# Changelog

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
