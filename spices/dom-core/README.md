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
- `dom/edit` -- editing a `Document` in place: create, insert, move, remove,
  replace and unwrap nodes, set attributes and text, rename, clone and import
  across documents, and `dom-new` to build one from nothing. Every edit is
  validated, so a serialized tree cannot carry structure the edits did not
  make.
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

## Editing

`dom/edit` changes a tree in place. The arena keeps that cheap and safe:
nothing is freed before `dom-free`, so a node taken out with `node-remove!`
is detached rather than dangling, and a `cstr` read before `set-attr!` still
reads the old value. The garbage this leaves is counted by
`dom-garbage-bytes`; `dom-compact` copies the live tree into a fresh
`Document`.

Every edit is checked first, and a refused one changes nothing: it returns an
`EditError` (`BadName`, `BadContent`, `BadPlacement`, ...). The checks cover
what the serializer writes verbatim -- names, comment bodies, PI data, and
the text of `<script>` and `<style>` -- so an element named
`x onload=alert(1)` or a script holding `</script>` is refused instead of
injected. They are about structure, not meaning: an `onclick` attribute or a
`<script>` is still a valid edit. Editing is not sanitizing.

```turmeric
(import dom/ownstr :refer [to-cstr release])
(import dom/core   :refer [dom-node dom-free dom-serialize])
(import dom/edit   :refer [HtmlFlavor dom-new create-element create-text
                           node-append! set-attr!])

(defn main [] : int
  (let [d (dom-new (HtmlFlavor))]
    (match (create-element d "li")
      (Ok li) (do (set-attr! li "class" "item")
                  (match (create-text d "<b>text, not markup</b>")
                    (Ok t)  (node-append! li t)
                    (Err _) (Ok nil))
                  (node-append! (dom-node d) li))
      (Err _) (Ok nil))
    (let [s (dom-serialize (dom-node d))]
      (println (to-cstr s))   ; <li class="item">&lt;b&gt;text, not markup&lt;/b&gt;</li>
      (release s))
    (dom-free d))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/ownstr :refer [to-cstr release]
import dom/core   :refer [dom-node dom-free dom-serialize]
import dom/edit   :refer [HtmlFlavor dom-new create-element create-text node-append! set-attr!]

defn main [] : int
  let [d dom-new(HtmlFlavor())]
    (match create-element(d "li")
      (Ok li) (do set-attr!(li "class" "item")
                  (match create-text(d "<b>text, not markup</b>")
                    (Ok t)  node-append!(li t)
                    (Err _) Ok(nil))
                  node-append!(dom-node(d) li))
      (Err _) Ok(nil))
    let [s dom-serialize(dom-node(d))]
      println(to-cstr(s))
      release(s)
    dom-free(d)
  0
```

## API

| Module | Exports |
|--------|---------|
| `dom/core` | `Document`, `Node`, `NodeKind` (`DocumentNode` `ElementNode` `TextNode` `CommentNode` `CdataNode` `PiNode` `DoctypeNode`), `SourcePos`, `ParseError`, `ParseErrorKind` (`Syntax` `Limit` `Refused` `Io`), `syntax-kind`, `limit-kind`, `refused-kind`, `io-kind`, `dom-root`, `dom-node`, `dom-free`, `dom-serialize`, `dom-equal?`, `dom-node-count`, `dom-live-bytes`, `node-kind`, `node-element?`, `node-name`, `node-local-name`, `node-ns-uri`, `node-value`, `node-text`, `node-attr`, `node-attr-count`, `node-attr-name-at`, `node-attr-value-at`, `node-children`, `node-child-count`, `node-first-child`, `node-last-child`, `node-next-sibling`, `node-prev-sibling`, `node-parent`, `node-document`, `node-pos`, `node-depth`, `node-matches?`, `find-all`, `find-first`, `parse-error-message`, `parse-error-release`, `parse-error-pos` |
| `dom/edit` | `DomFlavor` (`XmlFlavor` `HtmlFlavor`), `EditError` (`WrongDocument` `WouldCycle` `NotAContainer` `BadPlacement` `Detached` `WrongKind` `BadName` `BadContent` `OutOfMemory`), `edit-error-message`, `dom-new`, `dom-flavor`, `create-element`, `create-element-ns`, `create-text`, `create-comment`, `create-cdata`, `create-pi`, `node-import`, `node-clone`, `node-append!`, `node-prepend!`, `node-insert-before!`, `node-insert-after!`, `node-replace!`, `node-remove!`, `node-unwrap!`, `set-attr!`, `set-attr-ns!`, `remove-attr!`, `set-text!`, `set-value!`, `rename!`, `rename-ns!`, `node-normalize!`, `dom-garbage-bytes`, `dom-compact`, `node-attached?` |
| `dom/event` | `SaxEvent` (`StartElement` `EndElement` `Chars` `Comment` `Pi` `StartCdata` `EndCdata` `Doctype`), `SaxControl` (`Continue` `Stop`), `Attr`, `attrs-get`, `sax-event-release`, `sax-dispatch` |
| `dom/ownstr` | `String`, `from-cstr`, `adopt-cstr`, `to-cstr`, `retain`, `release`, `str-len`, `string=?` |

The [XML guide](../../docs/guides/xml-guide.md) documents the node API and the
event stream in depth.
