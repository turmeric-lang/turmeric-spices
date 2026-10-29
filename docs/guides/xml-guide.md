---
title: XML Parsing (SAX and DOM)
category: Parsing
description: Streaming SAX and in-memory DOM parsing of XML with tur-xml -- choosing between them, the shared node API, push parsing, namespaces, errors, and the safe defaults for untrusted input
audience: developers reading feeds, config files, SVG, sitemaps or any other XML from Turmeric
since: xml v0.1.0
---

# tur-xml Guide

`tur-xml` parses XML with [expat](https://libexpat.github.io/) and gives you
two ways to read the result: a stream of events (SAX) or a tree (DOM). Both
speak the vocabulary of [`tur-dom-core`](../../spices/dom-core/README.md), which
[`tur-html`](html-guide.md) shares, so what you learn here about nodes and
events carries over to HTML.

1. [Choosing SAX or DOM](#1-choosing-sax-or-dom)
2. [The DOM](#2-the-dom)
3. [SAX](#3-sax)
4. [Push parsing](#4-push-parsing)
5. [Namespaces](#5-namespaces)
6. [Options and security](#6-options-and-security)
7. [Errors](#7-errors)
8. [Encodings](#8-encodings)
9. [Editing and streaming transforms](#9-editing-and-streaming-transforms)
10. [Limitations](#10-limitations)
11. [Markup literals](#11-markup-literals)

---

## 0. Installing the spice

```turmeric no-check
:spices #map{
  "xml" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "main"
             :subdir "spices/xml"}
}
```

`tur fetch` builds expat 2.9.0. `tur-dom-core` is pulled in automatically.

---

## 1. Choosing SAX or DOM

| | SAX (`xml/sax`) | DOM (`xml/dom`) |
|---|---|---|
| Memory | constant: one 64 KiB chunk's events plus the open-element depth | the whole document, in one arena |
| Access | forward only, one event at a time | random: parents, siblings, queries |
| Early exit | yes -- return `(Stop)` | no -- the whole input is parsed |
| Streaming input | yes -- the push parser takes chunks as they arrive | needs the whole input |
| Output | your handler builds what it needs | a `Document` you can serialize back |
| Typical use | big dumps, logs, feeds you scan once, sockets | config files, SVG, documents you edit-and-inspect |

Rules of thumb:

- **Reach for SAX** when the input may be large or unbounded, when you want
  only a few elements out of it, when you can stop early, or when the input
  arrives in pieces (a socket, a pipe). A 36 MB file streams through SAX in a
  4 MB process.
- **Reach for DOM** when you need to look around -- up to a parent, across to
  a sibling, back to an earlier element -- or to serialize a document again.
  The tree costs roughly the size of the input plus per-node overhead.
- **Both at once is fine.** The DOM is built from the SAX layer's own event
  queue, so the two always agree on what a document contains: anything SAX
  accepts, DOM builds; anything SAX rejects, DOM rejects with the same error.

---

## 2. The DOM

### Parsing and freeing

`dom-parse-string`, `dom-parse-file` and their `-opts` variants return a
`(Result Document ParseError)`. A `Document` owns everything in it; release it
with `dom-free`:

```turmeric no-check
(match (dom-parse-file "config.xml")
  (Ok d)  (do (use-the-tree d)
              (dom-free d))
  (Err e) (do (println (parse-error-message e))
              (parse-error-release e)))
```

### Navigating

`dom-root` is the document element; `dom-node` is the invisible document node
above it, whose children are the doctype, top-level comments and PIs, and the
root. Every navigation function returns an `(Option Node)` -- none at the
edges -- except `node-children`, which returns a `(Cons Node)` list:

| Function | Returns |
|---|---|
| `node-parent`, `node-first-child`, `node-last-child`, `node-next-sibling`, `node-prev-sibling` | `(Option Node)` |
| `node-children` | `(Cons Node)`, in document order |
| `node-child-count`, `node-depth` | `int` |
| `node-kind` | `NodeKind`: `DocumentNode`, `ElementNode`, `TextNode`, `CommentNode`, `CdataNode`, `PiNode`, `DoctypeNode` |
| `node-document` | the owning `Document` |
| `node-pos` | `(Option SourcePos)` -- 1-based line and byte column where the node started |

`node-children` allocates a list cell per child; for one pass over a large
node, `node-first-child` and `node-next-sibling` walk without allocating.

### Names, text and attributes -- borrowed or owned

The type tells you who owns a string:

- **Borrowed `cstr`** -- `node-name`, `node-local-name`, `node-ns-uri`,
  `node-value`, `node-attr`, `node-attr-name-at`, `node-attr-value-at`. No
  allocation; valid until `dom-free`.
- **Owned `String`** -- `node-text` (the concatenated text of a subtree, DOM
  `textContent`) and `dom-serialize`. Release with `release`.

```turmeric no-check
(match (find-first (dom-node d) "title")
  (Some t) (let [s (node-text t)]          ;; owned
             (println (to-cstr s))
             (release s))
  (None)   nil)

(node-attr link "href")                    ;; (Option cstr), borrowed
```

`node-value` is a single text, comment, CDATA or PI node's own data; for an
element use `node-text`.

### Queries

`find-all n tag` returns every descendant element of `n` named `tag`, in
document order; `find-first` returns the first. `tag` can be the qualified
name as written (`atom:link`), the Clark form (`{http://www.w3.org/2005/Atom}link`,
see [Namespaces](#5-namespaces)), or `*` for any element. `node-matches?`
tests a single node the same way.

A small recursive walker over the result is the usual way to consume it --
define the helper above the function that calls it:

```turmeric
(import dom/core :refer [Node dom-node dom-free find-all node-attr])
(import xml/dom  :refer [dom-parse-string])

(defn print-ids [ns : (Cons Node)] : nil
  (if (tnil? ns)
    nil
    (do (match (node-attr (thead ns) "id")
          (Some id) (println id)
          (None)    nil)
        (print-ids (ttail ns)))))

(defn main [] : int
  (match (dom-parse-string "<r><e id=\"a\"/><g><e id=\"b\"/></g></r>")
    (Ok d)  (do (print-ids (find-all (dom-node d) "e"))
                (dom-free d))
    (Err _) nil)
  0)
```

```sweet-exp
#lang sweet-exp
import dom/core :refer [Node dom-node dom-free find-all node-attr]
import xml/dom  :refer [dom-parse-string]

defn print-ids [ns : (Cons Node)] : nil
  if tnil?(ns)
    nil
    do
      (match node-attr(thead(ns) "id")
        (Some id) println(id)
        (None)    nil)
      print-ids $ ttail ns

defn main [] : int
  (match dom-parse-string("<r><e id=\"a\"/><g><e id=\"b\"/></g></r>")
    (Ok d)  (do print-ids(find-all(dom-node(d) "e"))
                dom-free(d))
    (Err _) nil)
  0
```

### Serializing and comparing

`dom-serialize` writes a node and its subtree back as XML: empty elements as
`<x/>`, attribute values escaped (including tab, newline and carriage return,
so they survive attribute-value normalization), `]]>` split across two CDATA
sections. The XML declaration and the internal DTD subset are not reproduced.
`dom-equal?` compares two subtrees structurally (names, namespaces,
attributes in order, character data), ignoring source positions -- so
`parse -> serialize -> parse` compares equal, which the test suite checks.

### What the tree keeps

Elements with qualified names, namespace URIs and attributes (namespace
declarations included, as `xmlns` / `xmlns:p` attributes, so serialization
reproduces them); text, CDATA sections, comments and processing instructions,
each with its source position; the doctype's name and identifiers. Adjacent
text is one node. Parsing builds it; `dom/edit` changes it in place (see
[section 9](#9-editing-and-streaming-transforms)).

### Checking for leaks

`dom-live-bytes` is the arena held by every live `Document` in the process.
It returns to its earlier value once every document parsed since has been
freed -- a cheap check for a long-running service:

```turmeric no-check
(let [before (dom-live-bytes)]
  (handle-request)
  (when (not (= before (dom-live-bytes)))
    (println "a Document was not freed")))
```

---

## 3. SAX

A handler is a `(fn [SaxEvent] SaxControl)`. It sees one event at a time and
returns `(Continue)` or `(Stop)`:

| Event | Meaning |
|---|---|
| `(StartElement name attrs)` | an opening tag; `attrs` is a `(Cons Attr)` in document order |
| `(EndElement name)` | the matching close -- also for `<x/>` |
| `(Chars text)` | character data |
| `(Comment text)` | `<!--text-->` |
| `(Pi target data)` | `<?target data?>` |
| `(StartCdata)` / `(EndCdata)` | bracket the `Chars` of a CDATA section |
| `(Doctype name public system)` | `<!DOCTYPE ...>`; an absent identifier is the empty String |

`attrs-get` looks an attribute up by name in a `StartElement`'s list.

```turmeric no-check
(sax-parse-string src
  (fn [e : SaxEvent] : SaxControl
    (match e
      (StartElement name attrs) (do (println (to-cstr name)) (Continue))
      (Chars text)              (Continue)
      _                         (Continue))))
```

### Text is coalesced

expat splits character data at buffer boundaries, entity references and
line ends. `tur-xml` joins adjacent runs, so `<a>1 &lt; 2</a>` is one
`(Chars "1 < 2")`, and the events never depend on how the input happened to
be chunked.

### Event strings are borrowed for the handler call

Every `String` in an event is a copy out of the parser's buffer, released
when the handler returns. That keeps a stream at constant memory. To keep a
name or text past the handler, take a reference with `retain` and `release`
it later:

```turmeric no-check
(StartElement name _) (do (keep-it (retain name)) (Continue))
```

The events themselves, and their attribute lists, are reclaimed per chunk by
a region. A handler that stores an event (rather than a retained string)
somewhere longer-lived makes that region retire instead of rewind -- still
correct, just not reclaimed.

### Stopping early

Returning `(Stop)` ends the parse at once, and the result is still `(Ok nil)`
-- even if the input goes on to be malformed. Use it to find the first match
in a huge file.

### Driving many parses

Every SAX entry point calls your handler closure, so code that calls one is
CPS-converted, and the continuation frames of those handler calls are
reclaimed when the outermost *direct-style* call returns. A loop that parses
many documents inside one helper function keeps every iteration's frames
until the helper returns. Two habits keep this flat:

- drive a long loop of parses from `main` (which is direct), or from a
  function that `main` calls once per document;
- write feeding loops (`sax-feed!` per chunk) as `while` loops, not
  self-recursion -- a self-call through a CPS-converted function is not a
  jump.

---

## 4. Push parsing

When the input arrives in pieces, create a parser with the handler, feed it
chunks, and finish:

```turmeric no-check
(match (sax-parser-new handler)
  (Ok p)  (do (sax-feed! p "<feed><item>he")
              (sax-feed! p "llo</item>")
              (sax-feed! p "</feed>")
              (sax-finish! p)
              (sax-parser-free p))
  (Err e) (parse-error-release e))
```

Chunks may split the input anywhere -- inside a tag, a name, an entity
reference, a multi-byte UTF-8 sequence. The test suite feeds a document one
byte at a time and in random chunk sizes and checks the events are exactly
the one-shot parse's. `sax-feed!` returns an error as soon as the input seen
so far is malformed, and keeps returning it; after `(Stop)`, further chunks
are accepted and ignored. Ending the input inside the root element is an
error at `sax-finish!`.

---

## 5. Namespaces

With `namespaces` on (the default), element and attribute names in **events**
are in Clark form: `{uri}local` for a name in a namespace, plain `local` for
one that is not. `xmlns` declarations are consumed, not reported as
attributes. A document using a default namespace therefore reads as
`{http://www.w3.org/2005/Atom}entry`, which is the name that actually
identifies the element -- the prefix (or its absence) is a spelling.

In the **tree**, an element keeps its qualified name as written (`node-name`
gives `atom:entry`), its `node-local-name` and its `node-ns-uri`, and the
namespace declarations stay as attributes so serialization round-trips.
`find-all`, `find-first`, `node-matches?` and `node-attr` accept either
spelling: the qualified name as written, or the Clark form.

With `(with (default-parse-opts) [namespaces false])`, names are reported
exactly as written and `xmlns` attributes are ordinary attributes -- which is
also what a document that is XML 1.0 but not namespace-well-formed (an
attribute named `:`, say) needs.

---

## 6. Options and security

XML parsers are a classic attack surface, so `default-parse-opts` -- used by
every entry point without `-opts` -- is the safe configuration:

| `ParseOpts` field | Default | Effect |
|---|---|---|
| `allow-external-entities` | `false` | Never read a `SYSTEM` entity or the external DTD subset. A reference to an external entity fails with kind `Refused`; an external DTD subset is simply not read. |
| `max-expansion-ratio` | `100.0` | expat's amplification limit: entity expansion may produce at most this many output bytes per input byte (once past expat's 8 MiB activation threshold). The billion-laughs document fails with kind `Limit`. |
| `max-depth` | `256` | Elements nested deeper fail with kind `Limit` (libxml2's default). `0` disables. |
| `max-bytes` | `64 MiB` | Longer input fails with kind `Limit`. `0` disables -- do that to stream a huge file through SAX. |
| `namespaces` | `true` | See [Namespaces](#5-namespaces). |

`ParseOpts` is a `^copy` struct; change one field with `with`:

```turmeric no-check
(sax-parse-file-opts "dump.xml" (with (default-parse-opts) [max-bytes 0]) handler)
```

**Turning on `allow-external-entities`** lets a document read any file the
process can read, which is exactly the XXE attack -- only do it for trusted
input. Even then, only the local filesystem is used (`file://` URLs and
relative paths, resolved against the document's directory); an `http://`
system identifier is refused. Undeclared entities in a document whose
external subset was not read are dropped, as SAX2's `skippedEntity` does.

---

## 7. Errors

A failure is a `ParseError`:

| Field / function | Meaning |
|---|---|
| `.kind` | `Syntax` (not well-formed), `Limit` (a cap fired, or out of memory), `Refused` (an external entity with external entities off), `Io` (the file could not be read) |
| `parse-error-message` | expat's description (`"mismatched tag"`, `"undefined entity"`, ...) or ours; borrowed until `parse-error-release` |
| `.line`, `.col`, `parse-error-pos` | 1-based line and byte column; `0 0` for an `Io` error |
| `parse-error-release` | releases the owned message |

Every event before a syntax error is delivered before the `Err`. The message
is owned so it survives the parser; release it once you have reported it (a
discarded error keeps its 32-byte box regardless).

---

## 8. Encodings

expat decodes UTF-8, UTF-16 (detected from the byte-order mark),
ISO-8859-1 and US-ASCII, as declared in the XML declaration or detected from
the BOM, and always reports UTF-8. A `cstr` cannot hold UTF-16's NUL bytes, so
parse UTF-16 documents from a file (`sax-parse-file`, `dom-parse-file`).
Other declared encodings fail with `unknown encoding`. Columns count bytes,
not characters.

---

## 9. Editing and streaming transforms

There are two ways to change a document, matching the two ways to read one.

**A tree you hold**: `dom/edit` in `tur-dom-core` creates, inserts, removes,
renames and re-attributes nodes in place (see the
[dom-core README](../../spices/dom-core/README.md#editing)). Every edit is
checked first -- a name cannot carry `x onload=...`, a comment cannot hold
`--` -- and a refused edit changes nothing.

**A document too big to hold**: `xml/transform` runs each event through a
function that returns the events to write in its place, and serializes those
as it goes. Rename, drop, wrap and inject at memory proportional to the
nesting depth; `sax-transform-file` streams file to file.

```turmeric
(import dom/ownstr    :refer [from-cstr release string=?])
(import dom/event     :refer [SaxEvent StartElement EndElement])
(import dom/writer    :refer [sax-keep])
(import xml/transform :refer [sax-transform])

;; <item> becomes <entry>; everything else passes through.
(defn rename [ev : SaxEvent] : (Cons SaxEvent)
  (match ev
    (StartElement n as) (if (string=? n "item")
                          (do (release n)
                              (tcons-of (StartElement (from-cstr "entry") as) (tnil)))
                          (sax-keep ev))
    (EndElement n)      (if (string=? n "item")
                          (do (release n)
                              (tcons-of (EndElement (from-cstr "entry")) (tnil)))
                          (sax-keep ev))
    _                   (sax-keep ev)))

(sax-transform "<list><item>a</item></list>" rename)
;; => (Ok "<list><entry>a</entry></list>")
```

The function **owns** the event it is given: return it to write it,
`sax-event-release` it to drop it. Every event it returns is written and then
released, so an event rebuilt from the input reuses its strings. The writer
checks what it cannot escape, as `dom/edit` does, plus the document's shape
(end tags that match, one root, no text outside it), and the transform stops
with `OutputError` at its first refusal.

With namespace processing on, names arrive in Clark form (`{uri}local`) and
the writer picks the prefixes: one already in scope, else a default
namespace for an element or a generated `nsN` for an attribute. Write an
`xmlns:p` attribute on the start tag to choose the prefix yourself. With
namespaces off, names pass through as written, and the identity transform
(`sax-keep`) reproduces `dom-serialize` of the same document byte for byte;
the test suite checks that over xmltest.

For other event sources -- a push parser fed from a socket, events you build
yourself -- use `dom/writer` directly: `sax-writer-new` or
`sax-writer-to-file`, then `sax-write!` per event or `sax-transform-handler`
as the parser's handler, and `sax-writer-finish!`.

---

## 10. Limitations

- No XPath, XSLT, XML Schema or DTD validation; expat is a non-validating
  parser (default attributes from the internal subset are applied, though).
- Notation declarations are not reported, and the tree drops the internal
  DTD subset and the XML declaration.
- `dom-serialize` writes a whole subtree; streaming output is for event
  streams (`xml/transform`, `dom/writer`), not for a tree.

The test suite runs James Clark's `xmltest` from the W3C XML Conformance Test
Suite (every `valid/sa` document's events must reproduce its canonical XML,
every `not-wf/sa` document must be rejected) and a mutation fuzz pass; see
`spices/xml/fixtures/`.

---

## 11. Markup literals

`xml/markup` adds `#xml{...}`, the XML counterpart of tur-html's
`#html{...}` (see the [HTML guide](html-guide.md#10-markup-literals) for the
full rules): markup with `${...}` holes, escaped by construction, giving an
`XmlFragment` that `xml->string` reads.

```turmeric no-check
(import xml/markup :refer [xml*])
(import dom/holes  :refer [XmlFragment xml->string])

(defn entry [title : cstr href : cstr] : XmlFragment
  #xml{<entry><title>${title}</title><link href=${href}/></entry>})
```

What is different from HTML:

- Names are case-sensitive and there is no raw text; CDATA sections and
  processing instructions take no holes. Holes in `on*`, `style` and
  `srcdoc` values stay compile errors here (browsers run them in SVG and in
  XHTML served as XML): the code-aware holes of `#html{...}` -- see the
  tur-html guide -- read HTML's rules for script and style text, which XML
  does not share.
- Attribute values escape tab, LF and CR as character references, which
  attribute-value normalization would otherwise fold; a `true` boolean
  attribute renders as `name="name"`.
- The first render parses the literal with expat, wrapped in a root
  element, so namespace prefixes must be declared inside the literal; a
  literal that is not well-formed panics then, with expat's message.
- A `raw-html` value must be well-formed XML content on its own, and is
  parsed and re-serialized before it is written. An `#html{...}` fragment
  (or an HTML `Node`) in an `#xml{...}` literal, or the other way round,
  panics.
