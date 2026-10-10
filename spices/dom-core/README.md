# tur-dom-core

The shared markup tree and SAX event vocabulary behind
[`tur-xml`](../xml/README.md) and [`tur-html`](../html/README.md).

## Overview

`tur-dom-core` is a Tier 2 spice (vendored C, no native dependencies). You
rarely depend on it directly -- `tur-xml` and `tur-html` bring it along -- but
its modules are what you import to read what they produce:

- `dom/core` -- `Document` (an owned tree) and `Node` (a borrowed handle into
  it), with navigation, attributes, text, `find-all` / `find-first`,
  `dom-serialize` (XML or HTML flavour, following the document) and
  `dom-equal?`; plus `ParseError`, the error type both parsers return.
- `dom/event` -- `SaxEvent`, `SaxControl` and `Attr`: the event stream both
  SAX front doors deliver.
- `dom/ownstr` -- `String`, the owned string type events and `node-text` hand
  back.

Because both parsers build into the same tree, a helper written against
`dom/core` works on XML and HTML alike.

## Ownership in one paragraph

A `Document` owns every node and every string in it, in one arena that
`dom-free` releases in one pass. A `Node` is a borrow, valid until `dom-free`.
Strings come back two ways and the type says which: a `cstr` from
`node-name`, `node-value` or `node-attr` is **borrowed** from the document (no
allocation); a `String` from `node-text` or `dom-serialize` is **owned** and
released with `release`. Every walk in the C layer is iterative, so no reader
can exhaust the stack on a deep tree. `dom-live-bytes` reports the arena held
by live documents, which is how the tests check that `dom-free` releases
everything.

## Example

```turmeric
(import dom/ownstr :refer [to-cstr release])
(import dom/core   :refer [dom-node dom-root dom-free dom-serialize node-name])
(import xml/dom    :refer [dom-parse-string])

(defn main [] : int
  (match (dom-parse-string "<a x=\"1\"><b/>text</a>")
    (Ok d)  (do (match (dom-root d)
                  (Some r) (match (node-name r) (Some n) (println n) (None) nil)
                  (None)   nil)
                (let [s (dom-serialize (dom-node d))]
                  (println (to-cstr s))
                  (release s))
                (dom-free d))
    (Err _) nil)
  0)
```

```sweet-exp
#lang sweet-exp
import dom/ownstr :refer [to-cstr release]
import dom/core   :refer [dom-node dom-root dom-free dom-serialize node-name]
import xml/dom    :refer [dom-parse-string]

defn main [] : int
  (match dom-parse-string("<a x=\"1\"><b/>text</a>")
    (Ok d)  (do (match dom-root(d)
                  (Some r) (match node-name(r) (Some n) println(n) (None) nil)
                  (None)   nil)
                (let [s dom-serialize(dom-node(d))]
                  println(to-cstr(s))
                  release(s))
                dom-free(d))
    (Err _) nil)
  0
```

## API

| Module | Exports |
|--------|---------|
| `dom/core` | `Document`, `Node`, `NodeKind` (`DocumentNode` `ElementNode` `TextNode` `CommentNode` `CdataNode` `PiNode` `DoctypeNode`), `SourcePos`, `ParseError`, `ParseErrorKind` (`Syntax` `Limit` `Refused` `Io`), `dom-root`, `dom-node`, `dom-free`, `dom-serialize`, `dom-equal?`, `dom-node-count`, `dom-live-bytes`, `node-kind`, `node-element?`, `node-name`, `node-local-name`, `node-ns-uri`, `node-value`, `node-text`, `node-attr`, `node-attr-count`, `node-attr-name-at`, `node-attr-value-at`, `node-children`, `node-child-count`, `node-first-child`, `node-last-child`, `node-next-sibling`, `node-prev-sibling`, `node-parent`, `node-document`, `node-pos`, `node-depth`, `node-matches?`, `find-all`, `find-first`, `parse-error-message`, `parse-error-release`, `parse-error-pos` |
| `dom/event` | `SaxEvent` (`StartElement` `EndElement` `Chars` `Comment` `Pi` `StartCdata` `EndCdata` `Doctype`), `SaxControl` (`Continue` `Stop`), `Attr`, `attrs-get`, `sax-event-release`, `sax-dispatch` |
| `dom/ownstr` | `String`, `from-cstr`, `adopt-cstr`, `to-cstr`, `retain`, `release`, `str-len`, `string=?` |

The [XML guide](../../docs/guides/xml-guide.md) documents the node API and the
event stream in depth.
