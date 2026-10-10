# tur-html

WHATWG HTML parsing for Turmeric via [lexbor](https://github.com/lexbor/lexbor):
the tree a browser would build from any input, plus a tokenizer-level event
stream.

## Overview

`tur-html` is a Tier 3 spice (`cmake-dep` -- fetches lexbor 3.0.1 and builds
only the five modules HTML parsing needs). HTML is forgiving where XML is
strict: the HTML standard defines a tree for **every** input, so unclosed
`<p>`s, misnested `<b><i>`, stray table content and a missing `<head>` come
back as the tree a browser builds, and `html-parse` never fails.

- **DOM** (`html/dom`) -- `html-parse` for documents, `html-parse-fragment`
  for markup parsed as the contents of a context element (innerHTML), and
  `html-parse-file`. The tree is the shared
  [`tur-dom-core`](../dom-core/README.md) one, so the node API, `find-all`,
  `node-text` and `dom-serialize` are the same as for XML.
- **Tokenizer events** (`html/sax`) -- tags as written, in the same `SaxEvent`
  vocabulary as [`tur-xml`](../xml/README.md)'s SAX. One-shot or push.

## Install

```turmeric no-check
:spices #map{
  "html" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "main"
              :subdir "spices/html"}
}
```

Then `tur fetch`. `tur-dom-core` comes along as a dependency.

## Quick start

Print every link target in a page:

```turmeric
(import dom/core :refer [Node dom-node dom-free find-all node-attr])
(import html/dom :refer [html-parse])

(defn print-hrefs [ns : (Cons Node)] : nil
  (if (tnil? ns)
    nil
    (do (match (node-attr (thead ns) "href")
          (Some h) (println h)
          (None)   nil)
        (print-hrefs (ttail ns)))))

(defn main [] : int
  (let [d (html-parse "<p>See <a href=/a>one</a><p>and <A HREF='/b'>two</A>")]
    (print-hrefs (find-all (dom-node d) "a"))
    (dom-free d))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/core :refer [Node dom-node dom-free find-all node-attr]
import html/dom :refer [html-parse]

defn print-hrefs [ns : (Cons Node)] : nil
  if tnil?(ns)
    nil
    do
      (match node-attr(thead(ns) "href")
        (Some h) println(h)
        (None)   nil)
      print-hrefs $ ttail ns

defn main [] : int
  let [d html-parse("<p>See <a href=/a>one</a><p>and <A HREF='/b'>two</A>")]
    print-hrefs $ find-all dom-node(d) "a"
    dom-free(d)
  0
```

## API

| Module | Exports |
|--------|---------|
| `html/dom` | `HtmlOpts`, `default-html-opts`, `html-parse`, `html-parse-opts`, `html-parse-fragment`, `html-parse-fragment-opts`, `html-parse-file`, `html-parse-file-opts`, `html-backend-version` |
| `html/sax` | `HtmlSaxParser`, `html-sax-parse-string`, `html-sax-parse-string-opts`, `html-sax-parser-new`, `html-sax-parser-new-opts`, `html-sax-feed!`, `html-sax-finish!`, `html-sax-parser-free` |

The [HTML guide](../../docs/guides/html-guide.md) covers both, the tree's
shape (namespaces, templates, the depth cap) and how the tokenizer stream
differs from the tree.

## Parsing is not sanitizing

`html-parse` followed by `dom-serialize` produces well-formed markup, **not
safe markup**: every `<script>`, inline event handler and `javascript:` URL in
the input survives the round trip. Never embed untrusted HTML on the strength
of having parsed it. A sanitizer is a separate (planned) piece of work.

## Limits

`HtmlOpts` has `max-depth` (default 512) and `max-bytes` (default 64 MiB).
Depth is capped the way Blink caps it: an element nested deeper than the cap
is attached beside its parent instead of under it, so nothing is dropped and
no walker ever sees a deeper tree. `html-parse` and `html-parse-fragment`
apply the depth cap and no byte cap (the input is already in memory); the
`-opts` and file entry points apply both and return a `Result`.

## Testing

`tests/html5lib.tur` runs the html5lib tree-construction tests (vendored under
`tests/data/html5lib/`, MIT, about 1700 cases), comparing an html5lib-format
dump of the dom-core tree with the expected one -- so it checks lexbor's tree
construction and the copy into dom-core together. Five tests newer than lexbor
3.0.1 are listed as known failures, and the suite fails if one of them starts
passing. `tests/` also covers a tag-soup corpus, fragments, the depth cap,
options and files, and the token stream (including push parsing one byte at a
time). `fixtures/run.sh` runs a mutation fuzz pass.
