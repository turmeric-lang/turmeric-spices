# tur-xml

XML parsing for Turmeric via [expat](https://libexpat.github.io/): a streaming
SAX API (one-shot and push) and a DOM built on it, with safe defaults for
untrusted input.

## Overview

`tur-xml` is a Tier 3 spice (`cmake-dep` -- fetches and builds expat 2.9.0).
It answers two different needs:

- **SAX** (`xml/sax`) -- a handler sees one event at a time: start and end
  tags, text, comments, PIs, CDATA brackets, the doctype. Memory stays flat
  whatever the document size (a 36 MB file streams in a 4 MB process), and the
  handler can stop early. The push parser takes input in chunks as it arrives
  and delivers exactly the events the one-shot parse would.
- **DOM** (`xml/dom`) -- the whole document as a tree you navigate and query
  with the shared [`tur-dom-core`](../dom-core/README.md) node API: children,
  parents, attributes, `find-all`, `node-text`, `dom-serialize`.

The tree and the event vocabulary are shared with
[`tur-html`](../html/README.md), so code written against `dom/core` and
`dom/event` reads both formats.

## Install

```turmeric no-check
:spices #map{
  "xml" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
             :ref    "main"
             :subdir "spices/xml"}
}
```

Then `tur fetch`. `tur-dom-core` comes along as a dependency.

## Quick start: DOM

```turmeric
(import dom/ownstr :refer [to-cstr release])
(import dom/core   :refer [Node dom-node dom-free find-all node-text
                           parse-error-message])
(import xml/dom    :refer [dom-parse-string])

(defn print-texts [ns : (Cons Node)] : nil
  (if (tnil? ns)
    nil
    (let [t (node-text (thead ns))]
      (println (to-cstr t))
      (release t)
      (print-texts (ttail ns)))))

(defn main [] : int
  (match (dom-parse-string "<rss><item><title>One</title></item></rss>")
    (Ok d)  (do (print-texts (find-all (dom-node d) "title"))
                (dom-free d))
    (Err e) (println (parse-error-message e)))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/ownstr :refer [to-cstr release]
import dom/core   :refer [Node dom-node dom-free find-all node-text parse-error-message]
import xml/dom    :refer [dom-parse-string]

defn print-texts [ns : (Cons Node)] : nil
  if tnil?(ns)
    nil
    let [t node-text(thead(ns))]
      println $ to-cstr t
      release(t)
      print-texts $ ttail ns

defn main [] : int
  (match dom-parse-string("<rss><item><title>One</title></item></rss>")
    (Ok d)  (do print-texts(find-all(dom-node(d) "title"))
                dom-free(d))
    (Err e) println(parse-error-message(e)))
  0
```

## Quick start: SAX

Count `<item>`s, stopping after the first hundred:

```turmeric
(import dom/ownstr :refer [string=?])
(import dom/event  :refer [SaxEvent SaxControl StartElement Continue Stop])
(import xml/sax    :refer [sax-parse-file-opts default-parse-opts])

(defn main [] : int
  (let [^mut items 0]
    (sax-parse-file-opts "feed.xml" (with (default-parse-opts) [max-bytes 0])
      (fn [e : SaxEvent] : SaxControl
        (match e
          (StartElement name _) (do (when (string=? name "item")
                                      (set! items (+ items 1)))
                                    (if (>= items 100) (Stop) (Continue)))
          _ (Continue))))
    (println items))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/ownstr :refer [string=?]
import dom/event  :refer [SaxEvent SaxControl StartElement Continue Stop]
import xml/sax    :refer [sax-parse-file-opts default-parse-opts]

defn main [] : int
  let [^mut items 0]
    sax-parse-file-opts "feed.xml" with(default-parse-opts() [max-bytes 0])
      (fn [e : SaxEvent] : SaxControl
        (match e
          (StartElement name _) (do (when string=?(name "item")
                                      set!(items {items + 1}))
                                    (if {items >= 100} (Stop) (Continue)))
          _ (Continue)))
    println(items)
  0
```

## API

| Module | Exports |
|--------|---------|
| `xml/sax` | `ParseOpts`, `default-parse-opts`, `sax-parse-string`, `sax-parse-string-opts`, `sax-parse-file`, `sax-parse-file-opts`, `SaxParser`, `sax-parser-new`, `sax-parser-new-opts`, `sax-feed!`, `sax-finish!`, `sax-parser-free`, `xml-backend-version` |
| `xml/dom` | `dom-parse-string`, `dom-parse-string-opts`, `dom-parse-file`, `dom-parse-file-opts` |

Events (`SaxEvent`, `SaxControl`, `Attr`) and the tree (`Document`, `Node`,
navigation, queries, serialization) come from
[`tur-dom-core`](../dom-core/README.md). The
[XML guide](../../docs/guides/xml-guide.md) covers both in depth, including
when to choose SAX over DOM.

## Safe by default

`default-parse-opts` is the safe configuration, and every entry point without
`-opts` uses it:

| Field | Default | Guards against |
|-------|---------|----------------|
| `allow-external-entities` | `false` | XXE: a `SYSTEM` entity or external DTD subset reading local files (or, in other parsers, URLs) |
| `max-expansion-ratio` | `100.0` | billion laughs / quadratic blowup (expat's amplification limit) |
| `max-depth` | `256` | stack exhaustion in a recursive walker over the tree |
| `max-bytes` | `64 MiB` | heap exhaustion building a tree; pass `0` to stream a huge file |
| `namespaces` | `true` | -- (names in Clark form `{uri}local`) |

A reference to an external entity is refused with a `Refused` error rather than
silently dropped. Even with `allow-external-entities` on, only local files are
read -- never a URL.

## Errors

Failures are a `ParseError` with a `kind` (`Syntax`, `Limit`, `Refused`, `Io`),
an owned message (`parse-error-message`, `parse-error-release`) and a 1-based
line and byte column. Every event before a syntax error is delivered first.

## Testing

`tests/` covers every error class, the limits, entities, CDATA, namespaces,
UTF-16 and Latin-1 input, early stop, push parsing one byte at a time and in
random chunks, the DOM node API, serialization, round trips and `dom-free`
accounting. `fixtures/run.sh` runs James Clark's `xmltest` from the W3C XML
Conformance Test Suite -- every `valid/sa` document's events must reproduce its
canonical XML byte for byte, and every `not-wf/sa` document must be rejected --
and a mutation fuzz pass. The suite is fetched from W3C (and checked against a
pinned SHA-256) rather than vendored, because its licence only permits
redistributing the unmodified archive.
