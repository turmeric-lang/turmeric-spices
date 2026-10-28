# WPT selector tests (vendored slice)

`selectors.js` and `ParentNode-querySelector-All-content.html` are copied
unmodified from [web-platform-tests](https://github.com/web-platform-tests/wpt)
`dom/nodes/`, commit `93d85554e80df4816e5505cc8e93fd2867ce3e65` (master on
2026-10-28). They are 3-clause BSD; the licence is in `LICENSE` beside them.
The XHTML version of the document is in tur-xml's
`tests/data/wpt-selectors/`.

`selectors.tsv` is generated from `selectors.js` by `convert.js`
(`node convert.js > selectors.tsv`); do not edit it by hand. It keeps the rows
WPT runs through `querySelectorAll` (`TEST_QSA`), and marks the ones that use
a pseudo-class `dom/select` rejects on purpose -- `:link`, `:visited`,
`:target`, the UI states, `:lang()` and the pseudo-elements -- so the test can
check those are compile errors rather than empty results.

`tests/select.tur` (and tur-xml's) mirror WPT's
`ParentNode-querySelector-All.html` driver: the special elements built with
dom/edit, then the document, detached-element and in-document-element
contexts. WPT's DocumentFragment context has no counterpart in dom-core.
