---
title: HTML Parsing
category: Parsing
description: WHATWG HTML parsing with tur-html -- documents and fragments into the shared dom-core tree, the tree's shape, the depth cap, tokenizer-level events, and why parsing is not sanitizing
audience: developers scraping, transforming or inspecting HTML from Turmeric
since: html v0.1.0
---

# tur-html Guide

`tur-html` parses HTML with [lexbor](https://github.com/lexbor/lexbor), which
implements the WHATWG HTML parsing algorithm -- the one browsers use. The
result is the shared [`tur-dom-core`](../../spices/dom-core/README.md) tree,
read with the same node API as an XML document; the
[XML guide](xml-guide.md#2-the-dom) documents that API, and this guide covers
what is particular to HTML.

1. [Documents never fail](#1-documents-never-fail)
2. [Fragments](#2-fragments)
3. [The shape of the tree](#3-the-shape-of-the-tree)
4. [The depth cap](#4-the-depth-cap)
5. [Serializing (and why it is not sanitizing)](#5-serializing)
6. [Tokenizer events](#6-tokenizer-events)
7. [Options, files and encodings](#7-options-files-and-encodings)
8. [Editing and token-level transforms](#8-editing-and-token-level-transforms)
9. [CSS selectors](#9-css-selectors)

---

## 0. Installing the spice

```turmeric no-check
:spices #map{
  "html" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
              :ref    "main"
              :subdir "spices/html"}
}
```

`tur fetch` builds the HTML subset of lexbor 3.0.1. `tur-dom-core` is pulled
in automatically.

---

## 1. Documents never fail

XML is strict: a mismatched tag is fatal. HTML is the opposite -- the
standard defines a tree for every possible input -- so `html-parse` returns a
`Document`, not a `Result`:

```turmeric
(import dom/ownstr :refer [to-cstr release])
(import dom/core   :refer [dom-node dom-free dom-serialize])
(import html/dom   :refer [html-parse])

(defn main [] : int
  (let [d (html-parse "<title>T</title><p>one<p>two <b>bold <i>both</b> italic</i>")
        s (dom-serialize (dom-node d))]
    (println (to-cstr s))
    (release s)
    (dom-free d))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/ownstr :refer [to-cstr release]
import dom/core   :refer [dom-node dom-free dom-serialize]
import html/dom   :refer [html-parse]

defn main [] : int
  let [d html-parse("<title>T</title><p>one<p>two <b>bold <i>both</b> italic</i>")
       s dom-serialize(dom-node(d))]
    println $ to-cstr s
    release(s)
    dom-free(d)
  0
```

prints

```text
<html><head><title>T</title></head><body><p>one</p><p>two <b>bold <i>both</i></b><i> italic</i></p></body></html>
```

The implied `<html>`, `<head>` and `<body>` appear, the first `<p>` is closed
by the second, and the misnested `<b><i>` is repaired the way every browser
repairs it (the "adoption agency"). Tables get their `<tbody>`, text that
strays into a table is moved before it, a `<template>` at the start lands in
`<head>`, and so on. The test suite checks this against the html5lib
tree-construction tests.

---

## 2. Fragments

`html-parse-fragment context src` parses markup as the contents of a
`context` element -- the algorithm behind `innerHTML`. The context matters:

| Context | Markup | Result |
|---|---|---|
| `"tr"` | `<td>a<td>b` | two `<td>` cells |
| `"body"` | `<td>a<td>b` | the text `ab` (cells are not allowed there) |
| `"textarea"` | `<b>x</b>` | the text `<b>x</b>` |
| `"table"` | `<tr><td>x` | `<tbody><tr><td>x</td></tr></tbody>` -- the implied `<tbody>` |

The fragment's top-level nodes are the children of the returned document's
node -- there is no `<html>`/`<head>`/`<body>` wrapper -- and `dom-root` is
its first element, or none for a fragment that is all text. The context is
an HTML tag name; SVG and MathML contexts are not supported.

---

## 3. The shape of the tree

- **Names.** HTML element and attribute names are lower-case. SVG and MathML
  elements and attributes keep the case the parser adjusts them to
  (`foreignObject`, `viewBox`, `definitionURL`). Matching in `find-all`,
  `find-first`, `node-matches?` and `node-attr` ignores ASCII case in an HTML
  document, so `(find-all n "A")` finds `<a>`.
- **Namespaces.** `node-ns-uri` is the HTML, SVG or MathML namespace URI.
  Attributes are in no namespace except the adjusted `xlink:`, `xml:` and
  `xmlns` ones; `(node-attr a "{http://www.w3.org/1999/xlink}href")` finds
  `xlink:href`. The HTML parser never gives an element a prefix: `<x:y>` is an
  element whose local name is `x:y`.
- **Templates.** A `<template>`'s contents (a separate document fragment in
  the browser DOM) are the template element's children here.
- **No positions.** lexbor's tree does not record source positions, so
  `node-pos` is none for every HTML node.
- **Doctype.** Kept as a `DoctypeNode` with its name and identifiers.

---

## 4. The depth cap

Deeply nested input (`<div>` a million times) is a cheap way to exhaust a
recursive tree walker. `tur-html` caps the depth the way Blink does: an
element nested deeper than `max-depth` (512 by default) is attached **beside**
its parent instead of under it. Nothing is dropped -- every element and all
the text are still in the tree -- but no node is deeper than the cap. Every
walk inside dom-core is iterative anyway, so a 100 000-deep input parses
without trouble; the cap protects your own recursive code.

---

## 5. Serializing

`dom-serialize` on an HTML document follows the HTML fragment serialization
algorithm: void elements (`<br>`, `<img>`) have no end tag, the contents of
`<script>`, `<style>` and the other raw-text elements are written verbatim,
`&`, `<`, `>` and U+00A0 are escaped in text (`&nbsp;`), and `&` and `"` in
attribute values (plus `<` and `>`, as current browsers do).

**Parsing is not sanitizing.** A parsed and re-serialized document keeps
every `<script>`, every `onclick=` and every `javascript:` URL it came with.
The round trip makes markup well-formed, never safe. Do not embed untrusted
HTML on the strength of having parsed it; that needs a sanitizer, which is
separate (planned) work.

---

## 6. Tokenizer events

`html/sax` reports **tokens**, in the same `SaxEvent` vocabulary as
`tur-xml`'s SAX (see the [XML guide](xml-guide.md#3-sax) for handlers,
`Stop`, ownership and push parsing). It is the tokenizer alone, with no tree
construction behind it:

| Input | Events |
|---|---|
| `<p>a<p>b` | `StartElement p`, `Chars a`, `StartElement p`, `Chars b` -- no implied end tags |
| `<b><i></b></i>` | four events, in that order -- no repair |
| `<br>` | `StartElement br` only (a void element has no end tag) |
| `<br/>`, `<path .../>` | `StartElement` then `EndElement` |
| `<script>a<b></script>` | `StartElement script`, `Chars a<b>`, `EndElement script` |
| `<!DOCTYPE html>` | `Doctype html "" ""` |

Names are lower-case, character references are decoded in text and attribute
values, NUL becomes U+FFFD, and the contents of `<script>`, `<style>`,
`<title>`, `<textarea>`, `<xmp>`, `<iframe>`, `<noembed>`, `<noframes>` and
`<plaintext>` are text. That last switch is normally the tree builder's job;
here it is decided by tag name, so an SVG `<style>` is treated the same way.
Use the tokenizer for fast scans (every link, every `<meta>`), and
`html-parse` whenever you need the structure a browser would see.

The push tokenizer (`html-sax-parser-new`, `html-sax-feed!`,
`html-sax-finish!`) delivers the same events for any chunking. lexbor 3.0.1
mishandles a chunk boundary inside a few lookaheads (a `<![CDATA[`, a
DOCTYPE's `PUBLIC` / `SYSTEM`), so the push path holds back the tail from the
first `<` that no `>` has closed yet and feeds it with the next chunk.

---

## 7. Options, files and encodings

`HtmlOpts` has two fields: `max-depth` (512) and `max-bytes` (64 MiB). The
never-failing `html-parse` and `html-parse-fragment` apply the depth cap and
no byte cap -- the input is already in memory. The `-opts` variants and
`html-parse-file` apply both and return `(Result Document ParseError)`: kind
`Limit` past `max-bytes`, `Io` when a file cannot be read.

Input is UTF-8. lexbor's encoding sniffing (`<meta charset>`, BOMs) lives in
a module this spice does not build, so convert other encodings first.

---

## 8. Editing and token-level transforms

A parsed tree is edited with `tur-dom-core`'s `dom/edit` (see the
[dom-core README](../../spices/dom-core/README.md#editing)), and
`html/edit` adds the HTML-specific pieces: `set-inner-html!` parses markup in
an element's own context and puts it in place of the element's children,
checked like any other edit, so markup that would close a `<script>` early is
refused; `node-inner-html` and `node-outer-html` serialize.

`html/transform` is the streaming counterpart, and it works on **tokens**,
like `html/sax`: `html-sax-transform` runs every event through a function
that returns the events to write in its place, then serializes them. See the
[XML guide](xml-guide.md#9-editing-and-streaming-transforms) for the function's
ownership rule. The identity transform gives back markup that tokenizes to
the same events -- including a doctype's identifiers, so the quirks mode does
not change -- which for ordinary pages parses to the same tree too. Two
things to know:

- Since the stream has no tree behind it, end tags are written as the
  tokenizer saw them (missing, stray or misnested), and `<br/>` comes back
  as `<br/>`. A self-closed non-void element, `<div/>`, comes back as
  `<div></div>`: the two tokenize alike, but a parser keeps `<div/>` open.
- The writer refuses what it cannot write back as given, and the transform
  stops there with `OutputError`: a `<plaintext>` element (`BadName`), a
  comment that holds `<!--`, and `<script>` text holding `<!--` or a
  `</script` lookalike (`BadContent`). The test suite runs every html5lib
  input through the identity; about 9% of them -- that suite aims at exactly
  these edges -- are refused, the rest come back token for token.

For input too large to hold, plug `dom/writer`'s `sax-transform-handler` into
`html-sax-parser-new`, with a writer from `sax-writer-to-file`, and feed the
input in chunks.

---

## 9. CSS selectors

`tur-dom-core`'s `dom/select` runs CSS selectors over a parsed page; the
[XML guide](xml-guide.md#css-selectors) has the API. On an HTML document it
follows the HTML rules: type selectors and attribute names ignore ASCII case
on HTML elements (`DIV` finds `<div>`, but `foreignobject` does not find SVG's
`<foreignObject>`), and so do the values of the attributes HTML lists as
case-insensitive (`[type=TEXT]` finds `type="text"`; the `s` flag turns that
off). `#id` and `.class` are case-sensitive, as in a no-quirks document --
dom-core does not record quirks mode, so every document is treated as
no-quirks.

```turmeric
(import dom/core   :refer [Node dom-node dom-free node-text])
(import dom/ownstr :refer [to-cstr release])
(import dom/select :refer [query])
(import html/dom   :refer [html-parse])

(defn print-texts [ns : (Cons Node)] : nil
  (if (tnil? ns)
    nil
    (let [t (node-text (thead ns))]
      (println (to-cstr t))
      (release t)
      (print-texts (ttail ns)))))

(defn main [] : int
  (let [d (html-parse "<ul><li class=done>a<li>b<li class=done>c</ul>")]
    (match (query (dom-node d) "li:not(.done)")
      (Ok ns) (print-texts ns)
      (Err _) nil)
    (dom-free d))
  0)
```

```sweet-exp
#lang sweet-exp
import dom/core   :refer [Node dom-node dom-free node-text]
import dom/ownstr :refer [to-cstr release]
import dom/select :refer [query]
import html/dom   :refer [html-parse]

defn print-texts [ns : (Cons Node)] : nil
  if tnil?(ns)
    nil
    let [t node-text(thead(ns))]
      println $ to-cstr t
      release(t)
      print-texts $ ttail ns

defn main [] : int
  let [d html-parse("<ul><li class=done>a<li>b<li class=done>c</ul>")]
    (match query(dom-node(d) "li:not(.done)")
      (Ok ns) print-texts(ns)
      (Err _) nil)
    dom-free(d)
  0
```

The engine is checked against the
[web-platform-tests](https://github.com/web-platform-tests/wpt) table that
browsers' `querySelectorAll` is tested with (`tests/select.tur` here; the
XHTML run is tur-xml's), and against a naive reference matcher on random
trees (dom-core's `tests/select_fuzz.tur`).
