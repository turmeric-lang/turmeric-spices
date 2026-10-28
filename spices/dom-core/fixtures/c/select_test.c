/* fixtures/c/select_test.c -- C-level tests for c/dom/turdom_select.c
 * (xml-html-followups-plan follow-up C).
 *
 * Trees are built from a tiny markup subset by mk() below -- elements,
 * double-quoted attributes, text, `<x/>` -- through the same builder entry
 * points the parsers use, so the engine is tested without either parser.
 * The parsers' own suites (tur-html tests/select.tur, tur-xml
 * tests/select.tur) run the WPT tables over real documents.
 *
 * Built and run by fixtures/run.sh under ASan/UBSan.
 */

#include "dom/turdom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks = 0, fails = 0;

#define CHECK(cond, ...)                                             \
    do {                                                             \
        checks++;                                                    \
        if (!(cond)) {                                               \
            fails++;                                                 \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);     \
            fprintf(stderr, __VA_ARGS__);                            \
            fputc('\n', stderr);                                     \
        }                                                            \
    } while (0)

#define XHTML "http://www.w3.org/1999/xhtml"

/* ---- a tiny markup builder ------------------------------------------------ */

/* Element names may be `{uri}local` for a namespaced element (qname = local),
 * or `p:l@uri` for a prefixed one.  Attribute names likewise accept
 * `p:l@uri`.  In an HTML document a plain element goes in the XHTML
 * namespace. */
static void split_name(const char *raw, char *qname, char *uri, char *local) {
    uri[0] = '\0';
    if (raw[0] == '{') {
        const char *c = strchr(raw, '}');
        memcpy(uri, raw + 1, (size_t)(c - raw - 1));
        uri[c - raw - 1] = '\0';
        strcpy(qname, c + 1);
        strcpy(local, c + 1);
        return;
    }
    const char *at = strchr(raw, '@');
    if (at) {
        memcpy(qname, raw, (size_t)(at - raw));
        qname[at - raw] = '\0';
        strcpy(uri, at + 1);
        const char *colon = strchr(qname, ':');
        strcpy(local, colon ? colon + 1 : qname);
        return;
    }
    strcpy(qname, raw);
    strcpy(local, raw);
}

/* Length of a name in the markup: up to a space, `>` or `/>`. */
static size_t name_len(const char *p) {
    size_t n = 0;
    while (p[n] && p[n] != ' ' && p[n] != '>' && p[n] != '=' &&
           !(p[n] == '/' && p[n + 1] == '>'))
        n++;
    return n;
}

static turdom_doc *mk(int flavor, const char *src) {
    turdom_doc *d = turdom_doc_new(flavor);
    turdom_node *at = turdom_doc_node(d);
    const char *p = src;
    while (*p) {
        if (p[0] == '<' && p[1] == '/') {
            p = strchr(p, '>') + 1;
            at = turdom_parent(at);
        } else if (*p == '<') {
            p++;
            char raw[128], q[128], u[128], l[128];
            size_t n = name_len(p);
            memcpy(raw, p, n);
            raw[n] = '\0';
            p += n;
            split_name(raw, q, u, l);
            const char *uri = u[0] ? u : (flavor == TURDOM_FLAVOR_HTML ? XHTML : NULL);
            turdom_node *el = turdom_append_element(d, at, q, uri,
                                                    (uri || strcmp(q, l)) ? l : NULL, 0, 0);
            for (;;) {
                while (*p == ' ') p++;
                if (*p == '/' || *p == '>') break;
                size_t an = name_len(p);
                memcpy(raw, p, an);
                raw[an] = '\0';
                p += an;
                split_name(raw, q, u, l);
                const char *v = "";
                size_t vl = 0;
                char vb[256];
                if (*p == '=') {
                    p += 2;
                    const char *e = strchr(p, '"');
                    vl = (size_t)(e - p);
                    memcpy(vb, p, vl);
                    vb[vl] = '\0';
                    v = vb;
                    p = e + 1;
                }
                turdom_add_attr(d, el, q, u[0] ? u : NULL, u[0] ? l : NULL, v, vl);
            }
            if (*p == '/') { p += 2; } else { p++; at = el; }
        } else {
            size_t n = strcspn(p, "<");
            turdom_append_chars(d, at, TURDOM_TEXT, p, n, 0, 0);
            p += n;
        }
    }
    return d;
}

/* The ids of every match from root, space-separated ("-" for an element with
 * no id). */
static void ids_of(turdom_selector *s, const turdom_node *root, char *out, size_t cap) {
    out[0] = '\0';
    turdom_selector_reset(s);
    for (turdom_node *n = turdom_select_next(s, root, NULL); n;
         n = turdom_select_next(s, root, n)) {
        const char *id = turdom_attr_get(n, "id");
        if (out[0]) strncat(out, " ", cap - strlen(out) - 1);
        strncat(out, id ? id : "-", cap - strlen(out) - 1);
    }
}

/* select_prev must list the same nodes, back to front. */
static int prev_agrees(turdom_selector *s, const turdom_node *root) {
    turdom_node *fwd[512];
    int n = 0;
    turdom_selector_reset(s);
    for (turdom_node *x = turdom_select_next(s, root, NULL); x && n < 512;
         x = turdom_select_next(s, root, x))
        fwd[n++] = x;
    for (turdom_node *x = turdom_select_prev(s, root, NULL); x;
         x = turdom_select_prev(s, root, x))
        if (n <= 0 || fwd[--n] != x) return 0;
    return n == 0;
}

static void expect(turdom_doc *d, const turdom_nsmap *ns, const char *sel, const char *want) {
    turdom_selector *s = turdom_selector_compile(sel, ns);
    if (!s || turdom_selector_error(s)) {
        CHECK(0, "'%s' did not compile: %s", sel, s ? turdom_selector_error(s) : "NULL");
        turdom_selector_free(s);
        return;
    }
    char got[4096];
    ids_of(s, turdom_doc_node(d), got, sizeof got);
    CHECK(strcmp(got, want) == 0, "'%s': want [%s] got [%s]", sel, want, got);
    CHECK(prev_agrees(s, turdom_doc_node(d)), "'%s': select_prev disagrees with select_next", sel);
    turdom_selector_free(s);
}

static void expect_error(const char *sel, const char *needle, int64_t off) {
    turdom_selector *s = turdom_selector_compile(sel, NULL);
    const char *e = s ? turdom_selector_error(s) : NULL;
    CHECK(e != NULL, "'%s' compiled; want an error mentioning '%s'", sel, needle);
    if (e) {
        CHECK(strstr(e, needle) != NULL, "'%s': error [%s] does not mention '%s'", sel, e, needle);
        if (off >= 0)
            CHECK(turdom_selector_error_offset(s) == off, "'%s': error offset %lld, want %lld",
                  sel, (long long)turdom_selector_error_offset(s), (long long)off);
    }
    turdom_selector_free(s);
}

/* ---- the documents -------------------------------------------------------- */

static const char *HTML_DOC =
    "<html id=\"html\"><body id=\"body\">"
    "<div id=\"d1\" class=\"a b\" lang=\"en-GB\" title=\"Hello World\">"
    "<p id=\"p1\" class=\"a\">one</p>"
    "<p id=\"p2\"></p>"
    "<span id=\"s1\" data-x=\"\"><em id=\"e1\">x</em></span>"
    "<p id=\"p3\" class=\"B\">three</p>"
    "</div>"
    "<ul id=\"ul\"><li id=\"l1\"/><li id=\"l2\" class=\"x\"/><li id=\"l3\"/><li id=\"l4\" class=\"x\"/><li id=\"l5\"/></ul>"
    "<INPUT id=\"in\" TYPE=\"Text\"/>"
    "<svg id=\"svg\"><{http://www.w3.org/2000/svg}foreignObject id=\"fo\"/></svg>"
    "<div id=\"123\" class=\"a:b\"/>"
    "</body></html>";

static const char *XML_DOC =
    "<feed@urn:atom id=\"feed\">"
    "<entry@urn:atom id=\"en1\"><title@urn:atom id=\"t1\">T</title><Title id=\"t2\"/></entry>"
    "<x:entry@urn:other id=\"en2\" x:k@urn:other=\"v\"/>"
    "<a:b id=\"ab\"/>"
    "</feed>";

static void test_errors(void) {
    expect_error("", "empty selector", 0);
    expect_error("   ", "empty selector", 0);
    expect_error("[", "attribute name", 1);
    expect_error("]", "unexpected", 0);
    expect_error("(", "unexpected", 0);
    expect_error(")", "expected a selector", 0);
    expect_error(">", "expected a selector", 0);
    expect_error("#", "after '#'", 0);
    expect_error("div,", "expected a selector", 4);
    expect_error(",div", "expected a selector", 0);
    expect_error(".", "identifier", 1);
    expect_error(".5cm", "identifier", 1);
    expect_error("..test", "identifier", 1);
    expect_error("div % p", "unexpected '%'", 4);
    expect_error("div ++ p", "expected a selector", 5);
    expect_error("div ~~ p", "expected a selector", 5);
    expect_error("[*=test]", "attribute name", 1);
    expect_error("[class= space unquoted ]", "unknown attribute selector flag 'unquoted'", 14);
    expect_error("[a=b x]", "unknown attribute selector flag 'x'", 5);
    expect_error("div:example", "unknown pseudo-class ':example'", 3);
    expect_error("div::before", "pseudo-elements are not supported", 3);
    expect_error("p:first-line", "pseudo-elements are not supported", 1);
    expect_error(":hover", "dynamic or UI state", 0);
    expect_error("input:checked", "dynamic or UI state", 5);
    expect_error("a:visited", "dynamic or UI state", 1);
    expect_error(":lang(en)", "inherited state", 0);
    expect_error("ns|div", "unknown namespace prefix 'ns'", 0);
    expect_error(":not(ns|div)", "unknown namespace prefix 'ns'", 5);
    expect_error("^|div", "unexpected", 0);
    expect_error("a || b", "column combinator", 2);
    expect_error(":nth-child(+ n)", "An+B", 11);
    expect_error(":nth-child(2 n)", "expected 'of' or ')'", 13);
    expect_error(":nth-child(n 1)", "expected ')'", 13);
    expect_error(":nth-of-type(1 of p)", "expected ')'", 15);
    expect_error(":not()", "expected a selector", 5);
    expect_error(":has(:has(p))", "cannot be nested", 5);
    expect_error(":has", "takes an argument", 0);
    expect_error("p div.a#b[c]x", "type selector must come first", 12);
    expect_error("/* x", "unterminated comment", 0);
    /* Deep nesting is bounded. */
    char deep[400] = "";
    for (int i = 0; i < 40; i++) strcat(deep, ":not(");
    strcat(deep, "p");
    for (int i = 0; i < 40; i++) strcat(deep, ")");
    expect_error(deep, "nested too deeply", -1);
}

static void test_html(void) {
    turdom_doc *d = mk(TURDOM_FLAVOR_HTML, HTML_DOC);
    /* type, universal, id, class */
    expect(d, NULL, "p", "p1 p2 p3");
    expect(d, NULL, "P", "p1 p2 p3");
    expect(d, NULL, "#d1 > *", "p1 p2 s1 p3");
    expect(d, NULL, "#D1", "");
    expect(d, NULL, ".a", "d1 p1");
    expect(d, NULL, ".b", "d1");
    expect(d, NULL, ".B", "p3");
    expect(d, NULL, ".a.b", "d1");
    expect(d, NULL, "#\\31 23", "123");
    expect(d, NULL, ".a\\:b", "123");
    expect(d, NULL, "input", "in");
    expect(d, NULL, "foreignObject", "fo");
    expect(d, NULL, "foreignobject", "");
    /* attributes */
    expect(d, NULL, "[data-x]", "s1");
    expect(d, NULL, "[DATA-X]", "s1");
    expect(d, NULL, "[data-x=\"\"]", "s1");
    expect(d, NULL, "[title~=World]", "d1");
    expect(d, NULL, "[title~=world]", "");
    expect(d, NULL, "[title~=world i]", "d1");
    expect(d, NULL, "[title~=\"\"]", "");
    expect(d, NULL, "[lang|=en]", "d1");
    expect(d, NULL, "[lang|=EN]", "d1");       /* lang: case-insensitive value */
    expect(d, NULL, "[lang|=EN s]", "");
    expect(d, NULL, "[title^=Hell]", "d1");
    expect(d, NULL, "[title^=\"\"]", "");
    expect(d, NULL, "[title$=rld]", "d1");
    expect(d, NULL, "[title*=\"o W\"]", "d1");
    expect(d, NULL, "[type=text]", "in");      /* type: case-insensitive value */
    expect(d, NULL, "[class=b]", "");          /* class: not on the list */
    expect(d, NULL, "[class=B]", "p3");
    expect(d, NULL, "[class=b i]", "p3");
    expect(d, NULL, "[class='a b']", "d1");
    /* end of input closes strings, attribute selectors and functions */
    expect(d, NULL, "[class='a b", "d1");
    expect(d, NULL, "[title=\"Hello World\"", "d1");
    expect(d, NULL, "[data-x", "s1");
    expect(d, NULL, "p:not(.a", "p2 p3");
    expect(d, NULL, "li:nth-child(2n", "l2 l4");
    /* combinators */
    expect(d, NULL, "div p", "p1 p2 p3");
    expect(d, NULL, "body > div > p", "p1 p2 p3");
    expect(d, NULL, "p + span", "s1");
    expect(d, NULL, "p ~ p", "p2 p3");
    expect(d, NULL, "span ~ *", "p3");
    expect(d, NULL, "html em", "e1");
    expect(d, NULL, "div  >  p.a", "p1");
    expect(d, NULL, "div/* c */p", "p1 p2 p3");
    /* structural */
    expect(d, NULL, ":root", "html");
    expect(d, NULL, ":scope", "html");
    expect(d, NULL, "p:empty", "p2");
    expect(d, NULL, "li:first-child", "l1");
    expect(d, NULL, "li:last-child", "l5");
    expect(d, NULL, "em:only-child", "e1");
    expect(d, NULL, "#d1 > :first-of-type", "p1 s1");
    expect(d, NULL, "#d1 > :last-of-type", "s1 p3");
    expect(d, NULL, "#d1 > :only-of-type", "s1");
    expect(d, NULL, "li:nth-child(2n+1)", "l1 l3 l5");
    expect(d, NULL, "li:nth-child(odd)", "l1 l3 l5");
    expect(d, NULL, "li:nth-child(EVEN)", "l2 l4");
    expect(d, NULL, "li:nth-child(-n+2)", "l1 l2");
    expect(d, NULL, "li:nth-child(n+4)", "l4 l5");
    expect(d, NULL, "li:nth-child(3)", "l3");
    expect(d, NULL, "li:nth-child(0n+3)", "l3");
    expect(d, NULL, "li:nth-child( 2n - 1 )", "l1 l3 l5");
    expect(d, NULL, "li:nth-child(2n- 1)", "l1 l3 l5");
    expect(d, NULL, "li:nth-child(+n)", "l1 l2 l3 l4 l5");
    expect(d, NULL, "li:nth-last-child(2)", "l4");
    expect(d, NULL, "li:nth-child(1 of .x)", "l2");
    expect(d, NULL, "li:nth-child(2 of .x)", "l4");
    expect(d, NULL, "li:nth-last-child(1 of .x)", "l4");
    expect(d, NULL, "p:nth-of-type(2)", "p2");
    expect(d, NULL, "p:nth-last-of-type(1)", "p3");
    /* logical */
    expect(d, NULL, "p:not(.a)", "p2 p3");
    expect(d, NULL, "p:not(.a, :empty)", "p3");
    expect(d, NULL, "p:not(div > .a)", "p2 p3");
    expect(d, NULL, ":is(p, em).a", "p1");
    expect(d, NULL, ":where(#d1, #ul) > :first-child", "p1 l1");
    expect(d, NULL, ":is(span em)", "e1");
    expect(d, NULL, "div:has(> p.a)", "d1");
    expect(d, NULL, "div:has(em)", "d1");
    expect(d, NULL, "p:has(+ span)", "p2");
    expect(d, NULL, "p:has(~ p.B)", "p1 p2");
    expect(d, NULL, ":has(> li.x + li)", "ul");
    expect(d, NULL, "li:has(+ li.x):not(:has(~ li.x ~ li.x))", "l3");
    /* lists: document order, once each */
    expect(d, NULL, "p, div, p.a", "d1 p1 p2 p3 123");
    expect(d, NULL, "#l3, #l1", "l1 l3");
    /* namespaces */
    turdom_nsmap *ns = turdom_nsmap_new();
    turdom_nsmap_add(ns, "svg", "http://www.w3.org/2000/svg");
    turdom_nsmap_add(ns, "h", XHTML);
    expect(d, ns, "svg|foreignObject", "fo");
    expect(d, ns, "h|svg", "svg");
    expect(d, ns, "svg|*", "fo");
    expect(d, ns, "*|foreignObject", "fo");
    expect(d, ns, "|p", "");
    turdom_nsmap_add(ns, "", "http://www.w3.org/2000/svg");
    expect(d, ns, "*", "fo");
    expect(d, ns, "[id=fo]", "fo");
    expect(d, ns, "p", "");
    turdom_nsmap_free(ns);

    /* select from an element: candidates are its descendants, combinators may
     * look above it, :scope is the element */
    turdom_selector *s = turdom_selector_compile("body p", NULL);
    turdom_node *d1 = turdom_select_next(s, turdom_doc_node(d), NULL);
    d1 = turdom_parent(d1);
    char got[256];
    ids_of(s, d1, got, sizeof got);
    CHECK(strcmp(got, "p1 p2 p3") == 0, "scoped 'body p': got [%s]", got);
    turdom_selector_free(s);
    s = turdom_selector_compile(":scope > p", NULL);
    ids_of(s, d1, got, sizeof got);
    CHECK(strcmp(got, "p1 p2 p3") == 0, "':scope > p' from #d1: got [%s]", got);
    turdom_selector_free(s);

    /* matches and closest */
    s = turdom_selector_compile("div", NULL);
    turdom_selector *em = turdom_selector_compile("em", NULL);
    turdom_node *e1 = turdom_select_next(em, turdom_doc_node(d), NULL);
    CHECK(turdom_closest(s, e1) == d1, "closest div of #e1 is #d1");
    CHECK(turdom_selector_matches(s, d1, d1), "#d1 matches div");
    CHECK(!turdom_selector_matches(s, e1, e1), "#e1 does not match div");
    turdom_selector *sc = turdom_selector_compile(":scope", NULL);
    CHECK(turdom_closest(sc, e1) == e1, "closest :scope is the element itself");
    CHECK(turdom_closest(s, turdom_doc_node(d)) == NULL, "closest from the document node");
    turdom_selector_free(s);
    turdom_selector_free(em);
    turdom_selector_free(sc);
    /* a failed selector matches nothing */
    s = turdom_selector_compile("p:hover", NULL);
    CHECK(turdom_select_next(s, turdom_doc_node(d), NULL) == NULL, "a failed selector matches nothing");
    turdom_selector_free(s);
    turdom_doc_free(d);
}

static void test_xml(void) {
    turdom_doc *d = mk(TURDOM_FLAVOR_XML, XML_DOC);
    expect(d, NULL, "entry", "en1 en2");
    expect(d, NULL, "Entry", "");
    expect(d, NULL, "title", "t1");
    expect(d, NULL, "Title", "t2");
    expect(d, NULL, "a\\:b", "ab");
    expect(d, NULL, "x\\:entry", "en2");
    expect(d, NULL, "[k]", "");
    expect(d, NULL, "[*|k]", "en2");
    expect(d, NULL, "[x\\:k=v]", "en2");
    expect(d, NULL, ":root", "feed");
    expect(d, NULL, "|Title", "t2");
    turdom_nsmap *ns = turdom_nsmap_new();
    turdom_nsmap_add(ns, "atom", "urn:atom");
    turdom_nsmap_add(ns, "o", "urn:other");
    expect(d, ns, "atom|entry", "en1");
    expect(d, ns, "o|entry", "en2");
    expect(d, ns, "atom|entry > atom|title", "t1");
    expect(d, ns, "[o|k=v]", "en2");
    expect(d, ns, "[atom|k]", "");
    turdom_nsmap_add(ns, "", "urn:atom");
    expect(d, ns, "entry", "en1");
    expect(d, ns, "#en2", "");          /* implied * is in the default namespace */
    expect(d, ns, "*|*#en2", "en2");
    expect(d, ns, "a\\:b", "ab");       /* a qualified name ignores the default */
    turdom_nsmap_free(ns);
    turdom_doc_free(d);
}

/* Every element's css path selects exactly that element. */
static int path_round_trip(turdom_doc *d) {
    int bad = 0;
    turdom_node *top = turdom_doc_node(d);
    for (turdom_node *n = turdom_next_in(top, NULL); n; n = turdom_next_in(top, n)) {
        if (turdom_kind(n) != TURDOM_ELEMENT) continue;
        size_t len = 0;
        char *path = turdom_css_path(n, &len);
        turdom_selector *s = path ? turdom_selector_compile(path, NULL) : NULL;
        int ok = s && !turdom_selector_error(s);
        if (ok) {
            turdom_selector_reset(s);
            turdom_node *first = turdom_select_next(s, top, NULL);
            ok = first == n && turdom_select_next(s, top, first) == NULL;
        }
        if (!ok) {
            bad++;
            fprintf(stderr, "# path [%s] (%s) does not pick its element\n",
                    path ? path : "(null)", s && turdom_selector_error(s) ? turdom_selector_error(s) : "");
        }
        turdom_selector_free(s);
        free(path);
    }
    return bad;
}

static void test_paths(void) {
    turdom_doc *d = mk(TURDOM_FLAVOR_HTML, HTML_DOC);
    CHECK(path_round_trip(d) == 0, "html paths round-trip");
    size_t len;
    turdom_selector *s = turdom_selector_compile("#e1", NULL);
    turdom_node *e1 = turdom_select_next(s, turdom_doc_node(d), NULL);
    char *p = turdom_css_path(e1, &len);
    CHECK(p && strcmp(p, "#e1") == 0, "path of a unique id: [%s]", p ? p : "");
    free(p);
    turdom_selector_free(s);
    turdom_doc_free(d);

    d = mk(TURDOM_FLAVOR_XML, XML_DOC);
    CHECK(path_round_trip(d) == 0, "xml paths round-trip");
    turdom_doc_free(d);

    /* Duplicate ids are not used; escapes survive. */
    d = mk(TURDOM_FLAVOR_HTML,
           "<html><body><div id=\"x\"><p id=\"dup\"/><p id=\"dup\"/></div>"
           "<div id=\"x\"><p id=\"9 a\"/><b/><p id=\"-\"/><p class=\"q\"/></div></body></html>");
    CHECK(path_round_trip(d) == 0, "paths with duplicate and odd ids round-trip");
    s = turdom_selector_compile("p", NULL);
    turdom_node *first = turdom_select_next(s, turdom_doc_node(d), NULL);
    p = turdom_css_path(first, &len);
    CHECK(p && strcmp(p, "html:nth-child(1):root > body:nth-child(1) > div:nth-child(1) > p:nth-child(1)") == 0,
          "path through duplicate ids: [%s]", p ? p : "");
    free(p);
    turdom_selector_free(s);
    CHECK(turdom_css_path(turdom_doc_node(d), &len) == NULL, "no path for the document node");
    turdom_doc_free(d);
}

/* A deep chain makes `div:has(span)` quadratic: each div tests every div
 * below it. 3000 levels is ~4.5M candidate tests, past the 1M budget. */
static void test_has_budget(void) {
    int depth = 3000;
    size_t cap = (size_t)depth * 12 + 64;
    char *src = (char *)malloc(cap);
    src[0] = '\0';
    for (int i = 0; i < depth; i++) strcat(src, "<div>");
    for (int i = 0; i < depth; i++) strcat(src, "</div>");
    turdom_doc *d = mk(TURDOM_FLAVOR_XML, src);
    free(src);
    turdom_selector *s = turdom_selector_compile("div:has(span)", NULL);
    turdom_selector_reset(s);
    turdom_node *n = turdom_select_next(s, turdom_doc_node(d), NULL);
    CHECK(n == NULL, "no div has a span");
    CHECK(turdom_selector_budget_hit(s), "the pathological :has() hits the budget");
    turdom_selector_reset(s);
    CHECK(!turdom_selector_budget_hit(s), "reset clears budget_hit");
    turdom_selector_free(s);
    /* A deep descendant chain does not recurse on the depth. */
    s = turdom_selector_compile("div div div div div div div div div div div div", NULL);
    turdom_selector_reset(s);
    int count = 0;
    for (n = turdom_select_next(s, turdom_doc_node(d), NULL); n;
         n = turdom_select_next(s, turdom_doc_node(d), n))
        count++;
    CHECK(count == depth - 11, "deep descendant chain: %d matches", count);
    turdom_selector_free(s);
    turdom_doc_free(d);

    d = mk(TURDOM_FLAVOR_XML, "<a><div><span/></div><div/></a>");
    s = turdom_selector_compile("div:has(span)", NULL);
    turdom_selector_reset(s);
    CHECK(turdom_select_next(s, turdom_doc_node(d), NULL) != NULL, "small :has() matches");
    CHECK(!turdom_selector_budget_hit(s), "small :has() stays within the budget");
    turdom_selector_free(s);
    turdom_doc_free(d);
}

int main(void) {
    test_errors();
    test_html();
    test_xml();
    test_paths();
    test_has_budget();
    if (fails) {
        fprintf(stderr, "%d of %d checks failed\n", fails, checks);
        return 1;
    }
    printf("ok - turdom select: %d checks\n", checks);
    return 0;
}
