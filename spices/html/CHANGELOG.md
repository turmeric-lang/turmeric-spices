# Changelog

## 0.1.0

Initial release: WHATWG HTML parsing via lexbor 3.0.1 (turmeric
`docs/upcoming/spices/xml-html-parsers-plan.md`, phases P5-P7).

### Added

- `html/dom`: `html-parse` (never fails), `html-parse-fragment` (innerHTML
  with a context element), `html-parse-file`, and `-opts` variants, copied
  into the dom-core tree: template contents as children, adjusted foreign
  names, xlink/xml/xmlns attribute namespaces, and Blink-style depth
  flattening at `max-depth` (512).
- `html/sax`: tokenizer-level events in the dom-core vocabulary, one-shot and
  push, with RCDATA / RAWTEXT / script / PLAINTEXT switching by tag name.
- `cmake-deps/lexbor`: builds only lexbor's core, dom, ns, tag and html
  modules.

### Fixed upstream behaviour

- lexbor 3.0.1's tokenizer mishandles a chunk boundary inside a `<![CDATA[`
  or DOCTYPE keyword lookahead; the push path holds back the first unclosed
  `<` tail so chunking never changes the events.

### Tests

- The html5lib tree-construction tests (vendored, MIT), a tag-soup corpus,
  fragments, the depth cap, the token stream and push invariance;
  `fixtures/` runs a mutation fuzz pass.
