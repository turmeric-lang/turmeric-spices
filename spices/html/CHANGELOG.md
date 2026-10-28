# Changelog

## Unreleased

### Added

- `html/markup` (xml-html-followups-plan follow-up A1-A2): `#html{...}`
  markup literals with `${...}` holes, escaped by construction, rendering
  to a `Fragment` with no tree built. A hole's position picks what it may
  hold (a hole in a name, comment, `<script>`/`<style>` text or an
  `on*`/`style`/`srcdoc` value is a compile error), its type picks how it
  renders, and URL attributes are scheme-checked. Each literal is parsed
  once with lexbor, in `template` context, on its first render to check
  the holes landed where the macro said. `html*` is the macro behind it.
- `errors/run.sh`: the compile-error table for `#html{...}` holes.

## 0.2.0

### Added

- `html/edit` (xml-html-parsers-plan follow-up B3): `set-inner-html!` parses
  markup with the fragment algorithm in the element's own context and puts
  the result in place of its children -- imported into the element's
  document and placed with dom-core's `dom/edit` checks, so a refused call
  changes nothing (markup that would close a `<script>` early is
  `BadContent`; a void element is `BadPlacement`; an SVG element or an XML
  document is `WrongKind`). `node-inner-html` and `node-outer-html`.
- `tests/html5lib.tur` runs every fragment test a second time through
  `set-inner-html!`; `fixtures/edit-fuzz.tur` fuzzes `dom/edit` on HTML
  documents.
- `html/transform` (follow-up B4): `html-sax-transform` and
  `html-sax-transform-opts` run tokenizer-level events through a
  `(fn [SaxEvent] (Cons SaxEvent))` and serialize the result with a dom-core
  `SaxWriter`. The identity gives back markup that tokenizes to the same
  events, doctype identifiers included; `tests/html5lib.tur` checks that for
  every html5lib input, and that each input the writer refuses (`<plaintext>`,
  a comment holding `<!--`, raw text holding `<!--` or a `</script`
  lookalike) is refused for one of those reasons. The fuzz pass gains the
  property.

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
