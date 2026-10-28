# Changelog

## Unreleased

### Added

- `dom/holes` and `dom/markupc` (xml-html-followups-plan follow-up A1-A2):
  the shared halves of tur-html's `#html{...}` and tur-xml's `#xml{...}`
  markup literals. `dom/markupc` is the compile-time context tokenizer the
  literal macros import `:for-macros`: it finds the `${...}` holes, makes a
  hole in a tag or attribute name, a comment, raw text, a doctype, or an
  `on*` / `style` / `srcdoc` value a compile error naming the place, and
  picks the class each hole goes through. `dom/holes` has the classes --
  `ToChild`, `ToAttr`, `ToUrlAttr`, with instances for `cstr`, `String`,
  `int`, `bool`, `Fragment`, `XmlFragment`, `Node`, `RawHtml`,
  `TrustedUrl`, lists and `Option` -- the `raw-html` and `trusted-url`
  opt-outs, the URL scheme allowlist (`url-safe`, `srcset-check`),
  `html->string` / `xml->string` / `fragment-free`, and the renderer and the
  per-literal parser check both literals share.
- `c/dom/turmarkup.c`, a third `:c-source`: escaping, the URL check, the
  template walk and the per-literal check cache.

### Changed

- `sax-event-retain` returns the event it retained, so a handler can write
  `(f (sax-event-retain ev))`. It returned `nil` to sidestep a compiler bug
  (turmeric `heap-adt-word-slot-missing-cast`), which is fixed.
- The tur-xml and tur-html SAX drivers call their handler directly.
  `sax-dispatch` stays, as `(h ev)`, for callers that use it.
- Needs a turmeric newer than v0.65.5: the first release after it, or `main`
  from 2026-10-17 on.

## 0.2.0

### Added

- `dom/edit` (xml-html-parsers-plan follow-up B): editing a `Document` in
  place. `dom-new` / `dom-flavor`; `create-element`, `create-element-ns`,
  `create-text`, `create-comment`, `create-cdata`, `create-pi`;
  `node-clone` and `node-import` (deep copies, across documents);
  `node-append!`, `node-prepend!`, `node-insert-before!`,
  `node-insert-after!`, `node-replace!`, `node-remove!`, `node-unwrap!`;
  `set-attr!`, `set-attr-ns!`, `remove-attr!`, `set-text!`, `set-value!`,
  `rename!`, `rename-ns!`, `node-normalize!`; `dom-garbage-bytes`,
  `dom-compact`, `node-attached?`; and `EditError`.
- Every edit is validated and a refused one changes nothing. Names cannot
  carry extra structure (`x onload=...`); comments, PI data and HTML raw text
  cannot close themselves early (`-->`, `?>`, `</script>`); HTML void and
  raw-text elements keep their content model; HTML documents refuse CDATA
  and PIs (its parser produces neither); XML content must be XML Chars.
  Content a parser would not read back as written is refused too: `\r`
  where it cannot be escaped, and `]]>` in CDATA.
- The C layer (`c/dom/turdom.c`) gains the mutators behind it, iterative
  throughout, plus `turdom_check_*` validation and `turdom_edit_*` checked
  entry points. `fixtures/c/edit_test.c` tests them under ASan and UBSan
  (`fixtures/run.sh`).
- `dom/writer` (follow-up B4): `SaxWriter` serializes a stream of
  `SaxEvent`s with `dom-serialize`'s escaping -- in memory
  (`sax-writer-new`) or streamed to a file in 64 KiB writes
  (`sax-writer-to-file`) -- through `sax-write!`, `sax-write-all!`,
  `sax-writer-finish!` and `sax-writer-free`. It refuses, with a sticky
  `WriteError` (`Rejected EditError`, `IoFailed`), what it cannot write back
  as given: the `dom/edit` checks on names and bodies, a duplicate attribute,
  and in XML an end tag that does not match, a second root, text outside it.
  XML names may be Clark names; the writer declares prefixes, honouring an
  `xmlns` attribute on the same start tag. `sax-transform-handler` runs each
  event through a `(fn [SaxEvent] (Cons SaxEvent))` that owns it and writes
  what comes back; `sax-keep` is the identity. `TransformError`
  (`InputError`, `OutputError`) is the error of tur-xml's and tur-html's
  transforms.
- `sax-event-retain` (dom/event) takes a reference to every String in an
  event.
- The C layer gains `turdom_writer_*` and the `TURDOM_E_IO` code;
  `fixtures/c/writer_test.c` tests them (`fixtures/run.sh` now builds every
  C test).

### Changed

- `dom-serialize` of an XML document declares the namespace of a node that
  an edit created or renamed wherever its prefix is not already bound. A
  parsed tree carries its declarations as attributes and serializes exactly
  as before.
- The HTML serializer escapes `\r` as `&#13;`, as the XML one already did.
  A parsed HTML tree never holds one, so this only shows in edited content.
- `dom-node-count` counts detached nodes too: every node the arena holds.

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
