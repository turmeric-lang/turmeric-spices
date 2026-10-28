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
10. [Markup literals](#10-markup-literals)
11. [Hiccup: markup as a value](#11-hiccup-markup-as-a-value)

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

## 10. Markup literals

`html/markup` adds `#html{...}`: markup written as markup, with `${...}`
holes for values, escaped by construction. The result is a `Fragment`
(`dom/holes`), and `html->string` gives its markup.

```turmeric no-check
(import html/markup :refer [html*])
(import dom/holes   :refer [Fragment html->string])

(defn item [name : cstr href : cstr done : bool] : Fragment
  #html{<li class="item" data-done=${done}>
          <a href=${href}>${name}</a>
        </li>})

(defn page [items : (Cons Fragment)] : Fragment
  #html{<ul>${items}</ul>})
```

```sweet-exp
#lang sweet-exp
import html/markup :refer [html*]
import dom/holes   :refer [Fragment html->string]

defn item [name : cstr href : cstr done : bool] : Fragment
  #html{<li class="item" data-done=${done}>
          <a href=${href}>${name}</a>
        </li>}

defn page [items : (Cons Fragment)] : Fragment
  #html{<ul>${items}</ul>}
```

`(item "Q&A" "javascript:alert(1)" true)` renders the literal as written,
line breaks included, with the holes filled: `data-done` present (a
boolean attribute), `href="about:invalid#tur-unsafe"` (the scheme check) and
`Q&amp;A` (escaped text).

**Where a hole sits decides what it may hold; the value's type decides how
it renders.** The macro reads the literal the way the HTML tokenizer does:

| Hole | Accepts (through) | Renders |
|---|---|---|
| content | `cstr`, `String`, `int` (`to-child`) | escaped text |
| | `Fragment`, `(Cons Fragment)`, `Node` | spliced markup |
| | `RawHtml` (`raw-html`) | unescaped, parsed in the hole's element |
| | `(Option T)` | nothing for `None` |
| attribute value | `cstr`, `String`, `int` (`to-attr`) | escaped value |
| | `bool` | present or absent (an HTML boolean attribute) |
| | `(Option T)` | `None` drops the whole attribute |
| `href` `src` `action` `formaction` `xlink:href` `poster` `cite` `srcset` | `cstr`, `String` (`to-url-attr`) | scheme-checked, then escaped |
| | `TrustedUrl` (`trusted-url`) | escaped, not checked |

A value of the wrong kind for its place -- a `Fragment` in an attribute, a
`bool` in content -- has no instance, so it is a type error at the hole.

A hole anywhere a value could change the document's structure is a
**compile error naming the place**: in a tag or attribute name, right after
a `<`, in a comment or doctype, inside `<script>`, `<style>`, `<textarea>`,
`<title>` and the other raw-text elements, sharing an unquoted attribute
value with other text, and in the value of an `on*`, `style` or `srcdoc`
attribute -- the browser hands those values to a JavaScript, CSS or HTML
parser, which HTML escaping does not protect.

URLs are checked with an allowlist: a relative URL or one with scheme
`http`, `https`, `mailto` or `tel` passes, anything else (`javascript:`,
`data:`, ...) becomes `about:invalid#tur-unsafe`. `trusted-url` and
`raw-html` are the two opt-outs, both greppable; `raw-html` markup is still
parsed in the hole's own element, so it cannot close anything outside it,
but it is **not sanitized** -- never give it markup an untrusted party wrote.

A literal is parsed as the content of a `<template>`, so one that starts
with `<tr>`, `<td>`, `<option>` or `<col>` keeps that structure. A literal
that starts with `<!doctype html>` or `<html>` is a whole document; a
fragment with a top-level `<head>` or `<body>` is a compile error, because
a fragment parse drops them. The first time a literal renders it is parsed
once with lexbor and every hole is checked to land where the macro said
(a disagreement panics with the literal); rendering itself is string
concatenation, with no tree built.

Two things about the syntax:

- `#html{...}` exists in a file read *after* `html/markup` is loaded. Import
  it from the entry module, before the modules that use the literal; or put
  `#use-reader-macros "<path to html/markup.tur>"` at the top of the file; or
  call the macro directly, `(html* "<p>${name}</p>")`.
- The body is read raw up to the matching `}`, counting braces but not
  quotes: a `}` that closes nothing is written `\}` (also inside a string in
  a hole), a backslash `\\`. `${` always opens a hole.

A `Fragment` owns its markup; `fragment-free` releases one, which a server
rendering a page per request will want.

---

## 11. Hiccup: markup as a value

`html/hiccup` builds markup as a Turmeric value, Clojure-hiccup style:
`(hiccup [...])` lowers a literal to constructor calls building an `Html`
tree, which ordinary functions build, map, filter and `match` on before
`hiccup->string` renders it.

```turmeric
(import html/hiccup :refer [Html hiccup hiccup->string])

(defn item [name : cstr href : cstr done : bool] : Html
  (hiccup [:li.item #map{:data-done done}
            [:a #map{:href href} name]]))

(defn page [items : (Cons Html)] : cstr
  (hiccup->string (hiccup [:ul#menu items])))
```

```sweet-exp
#lang sweet-exp
import html/hiccup :refer [Html hiccup hiccup->string]

defn item [name : cstr href : cstr done : bool] : Html
  hiccup [:li.item #map{:data-done done} [:a #map{:href href} name]]

defn page [items : (Cons Html)] : cstr
  hiccup->string $ hiccup [:ul#menu items]
```

An element is `[name attrs? child...]`:

- **name** -- a keyword, with CSS-style shorthand for the id and classes
  (`:li.item#main`), or a string literal for a name with a colon
  (`["svg:rect" ...]`; a keyword splits at the colon). A computed name is a
  compile error: build that element with `tag` and the constructors.
- **attrs** -- a `#map{...}` with keyword or string keys. A bare `{...}` is
  curly-infix, not a map, and is a compile error pointing here. A literal
  value is written as given; any other value is a hole through `to-attr`
  (or `to-url-attr` for `href`, `src` and the other URL attributes), exactly
  as in `#html{...}`: a `bool` is a boolean attribute, `None` drops the
  attribute, a URL is scheme-checked. A map `:class` joins the shorthand
  classes. `:attrs*` splices a `(Cons Attr)` built at run time.
- **children** -- string and number literals are text; nested vectors are
  elements; anything else is a hole through `to-child`, which takes what
  `#html{...}`'s content holes take plus `Html` and lists of `Html`.

What the literal shows is checked at compile time: every name is a valid
name; a void element (`br`, `img`, ...) takes no children; a second id; a
dynamic value under `on*`, `style` or `srcdoc`; a `<script>` or `<style>`
body that is not one string literal, or that would end the element early;
and nesting the HTML parser would undo -- block content in a `<p>`, `<a>`
in `<a>`, `<form>` in `<form>`, interactive content in `<a>` or `<button>`,
anything but the table-structure elements (or text) directly in a
`<table>`, `<tbody>` or `<tr>`. A tree assembled at run time is not
checked for nesting.

`Html`'s constructors are public, so a tree can be built and taken apart by
hand; every field that could carry unsafe bytes is an opaque type with a
checking constructor, so any `Html` value renders safely: `tag` and
`attr-name` return `None` for a name that is not a name, `tag` refuses the
raw-text elements, `attr-name` refuses `on*`, `style` and `srcdoc`, and
`attr` scheme-checks a URL-bearing attribute's value.

`hiccup->string` writes what `#html{...}` would for the same markup -- the
test suite checks the two give byte-identical output across a hostile
corpus -- and either can hold the other: an `Html` value goes in an
`#html{...}` hole, a `Fragment` in a hiccup child.

`Html` nodes are heap-allocated and never freed; rendering a tree per
request belongs in a `with-region` bracket.
