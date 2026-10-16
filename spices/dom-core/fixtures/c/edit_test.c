/* fixtures/c/edit_test.c -- the C-level unit test for turdom's editing API
 * (xml-html-parsers-plan follow-up B1).  fixtures/run.sh builds it with
 * -fsanitize=address,undefined against c/dom/turdom.c and runs it.
 *
 * After every edit the tree's link invariants are re-checked, so a mutator
 * that leaves a dangling prev/next/parent/count fails here rather than in a
 * reader later.  Deep trees (100k levels) check that clone, import, compact
 * and normalize stay iterative. */

#include "dom/turdom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0, g_fails = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; printf("not ok - %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static const char *H = "http://www.w3.org/1999/xhtml";
static const char *SVG = "http://www.w3.org/2000/svg";

/* Every node under root (root included) has consistent parent/prev/next links
 * and a child count matching its list. */
static int links_ok(const turdom_node *root) {
    for (const turdom_node *n = root; n; n = turdom_next_in(root, n)) {
        int64_t k = 0;
        const turdom_node *prev = NULL;
        for (const turdom_node *c = turdom_first_child(n); c; c = turdom_next_sibling(c)) {
            if (turdom_parent(c) != n || turdom_prev_sibling(c) != prev) return 0;
            if (turdom_node_doc(c) != turdom_node_doc(n)) return 0;
            prev = c;
            k++;
        }
        if (turdom_last_child(n) != prev || turdom_child_count(n) != k) return 0;
    }
    return 1;
}

static int ser_is(const turdom_node *n, const char *want) {
    size_t len = 0;
    char *s = turdom_serialize(n, &len);
    int ok = s && strlen(want) == len && memcmp(s, want, len) == 0;
    if (!ok) printf("#   got:  %s\n#   want: %s\n", s ? s : "(null)", want);
    free(s);
    return ok;
}

static turdom_node *el(turdom_doc *d, const char *name) {
    return turdom_create_element(d, name, NULL, NULL);
}

static turdom_node *text(turdom_doc *d, const char *s) {
    return turdom_create_chars(d, TURDOM_TEXT, s, strlen(s));
}

static void test_build_and_order(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r"), *a = el(d, "a"), *b = el(d, "b"), *c = el(d, "c");
    CHECK(turdom_append(top, r) == TURDOM_OK);
    CHECK(turdom_append(r, b) == TURDOM_OK);
    CHECK(turdom_prepend(r, a) == TURDOM_OK);
    CHECK(turdom_insert_after(b, c) == TURDOM_OK);
    CHECK(turdom_insert_before(a, text(d, "t")) == TURDOM_OK);
    CHECK(ser_is(top, "<r>t<a/><b/><c/></r>"));
    CHECK(links_ok(top));
    /* moves: an attached node is moved, not copied */
    CHECK(turdom_append(a, c) == TURDOM_OK);
    CHECK(ser_is(top, "<r>t<a><c/></a><b/></r>"));
    CHECK(turdom_insert_before(b, b) == TURDOM_OK);        /* before itself */
    CHECK(turdom_insert_after(b, b) == TURDOM_OK);
    CHECK(turdom_prepend(r, turdom_first_child(r)) == TURDOM_OK);
    CHECK(ser_is(top, "<r>t<a><c/></a><b/></r>"));
    CHECK(turdom_node_count(d) == 5);
    CHECK(links_ok(top));
    turdom_doc_free(d);
}

static void test_errors(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_doc *o = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r"), *a = el(d, "a"), *t = text(d, "x");
    turdom_append(top, r);
    turdom_append(r, a);
    turdom_append(a, t);
    CHECK(turdom_append(r, el(o, "z")) == TURDOM_E_WRONG_DOC);
    CHECK(turdom_append(a, r) == TURDOM_E_CYCLE);
    CHECK(turdom_append(a, a) == TURDOM_E_CYCLE);
    CHECK(turdom_append(t, el(d, "q")) == TURDOM_E_NOT_CONTAINER);
    CHECK(turdom_append(top, el(d, "r2")) == TURDOM_E_HIERARCHY);   /* second root */
    CHECK(turdom_append(top, text(d, " ")) == TURDOM_E_HIERARCHY);  /* top-level text */
    CHECK(turdom_append(r, top) == TURDOM_E_HIERARCHY);             /* document node */
    turdom_node *dt = turdom_append_doctype(d, top, "r", NULL, NULL, 0, 0);
    CHECK(links_ok(top));
    /* that appended a doctype AFTER the root element; take it back out and
     * check the ordering rules */
    turdom_remove(dt);
    CHECK(turdom_append(top, dt) == TURDOM_E_HIERARCHY);   /* after root */
    CHECK(turdom_append(r, dt) == TURDOM_E_HIERARCHY);     /* in an element */
    CHECK(turdom_prepend(top, dt) == TURDOM_OK);
    CHECK(turdom_prepend(top, el(d, "r3")) == TURDOM_E_HIERARCHY);
    CHECK(turdom_insert_before(el(d, "loose"), el(d, "q")) == TURDOM_E_DETACHED);
    CHECK(turdom_insert_after(el(d, "loose"), el(d, "q")) == TURDOM_E_DETACHED);
    CHECK(turdom_set_attr(t, "x", NULL, NULL, "1", 1) == TURDOM_E_BAD_KIND);
    CHECK(turdom_set_value(r, "x", 1) == TURDOM_E_BAD_KIND);
    CHECK(turdom_set_text(top, "x", 1) == TURDOM_E_BAD_KIND);
    CHECK(turdom_rename(t, "x", NULL, NULL) == TURDOM_E_BAD_KIND);
    CHECK(turdom_clone(top) == NULL);
    CHECK(turdom_import(o, top) == NULL);
    CHECK(turdom_create_chars(d, TURDOM_ELEMENT, "x", 1) == NULL);
    CHECK(ser_is(top, "<!DOCTYPE r><r><a>x</a></r>"));
    CHECK(links_ok(top));
    turdom_doc_free(o);
    turdom_doc_free(d);
}

static void test_remove_replace_unwrap(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r"), *w = el(d, "w"), *a = el(d, "a");
    turdom_append(top, r);
    turdom_append(r, w);
    turdom_append(w, a);
    turdom_append(w, text(d, "t"));
    CHECK(turdom_attached(a));
    /* remove: detached, still readable, re-insertable; idempotent */
    turdom_remove(w);
    turdom_remove(w);
    CHECK(!turdom_attached(w) && !turdom_attached(a));
    CHECK(turdom_parent(w) == NULL && turdom_child_count(r) == 0);
    CHECK(strcmp(turdom_name(a), "a") == 0);
    CHECK(ser_is(w, "<w><a/>t</w>"));
    CHECK(turdom_append(r, w) == TURDOM_OK);
    /* unwrap: children take w's place */
    CHECK(turdom_unwrap(w) == TURDOM_OK);
    CHECK(ser_is(top, "<r><a/>t</r>"));
    CHECK(turdom_parent(w) == NULL && turdom_child_count(w) == 0);
    /* unwrapping the root with text children would put text at the top level */
    CHECK(turdom_unwrap(r) == TURDOM_E_HIERARCHY);
    CHECK(ser_is(top, "<r><a/>t</r>"));
    CHECK(turdom_unwrap(w) == TURDOM_E_DETACHED);
    /* replace: the root element by another element */
    turdom_node *r2 = el(d, "r2");
    CHECK(turdom_replace(r, r2) == TURDOM_OK);
    CHECK(ser_is(top, "<r2/>"));
    CHECK(turdom_root_element(d) == r2);
    /* replace a node by its own ancestor: refused, tree unchanged */
    turdom_node *x = el(d, "x"), *y = el(d, "y");
    turdom_append(r2, x);
    turdom_append(x, y);
    CHECK(turdom_replace(y, r2) == TURDOM_E_CYCLE);
    CHECK(ser_is(top, "<r2><x><y/></x></r2>"));
    /* replace by its own descendant: allowed (the DOM allows it) */
    CHECK(turdom_replace(x, y) == TURDOM_OK);
    CHECK(ser_is(top, "<r2><y/></r2>"));
    CHECK(turdom_replace(y, y) == TURDOM_OK);
    CHECK(links_ok(top));
    turdom_doc_free(d);
}

static void test_content(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r");
    turdom_append(top, r);
    CHECK(turdom_garbage_bytes(d) == 0);
    CHECK(turdom_set_attr(r, "a", NULL, NULL, "1", 1) == TURDOM_OK);
    CHECK(turdom_set_attr(r, "b", NULL, NULL, "2", 1) == TURDOM_OK);
    const char *old = turdom_attr_get(r, "a");
    CHECK(turdom_set_attr(r, "a", NULL, NULL, "3", 1) == TURDOM_OK);
    CHECK(strcmp(old, "1") == 0);                 /* the old bytes are still there */
    CHECK(strcmp(turdom_attr_get(r, "a"), "3") == 0);
    CHECK(turdom_garbage_bytes(d) > 0);
    CHECK(ser_is(top, "<r a=\"3\" b=\"2\"/>"));
    CHECK(turdom_remove_attr(r, "b") == 1);       /* the tail */
    CHECK(turdom_remove_attr(r, "b") == 0);
    CHECK(turdom_set_attr(r, "c", NULL, NULL, "4", 1) == TURDOM_OK);  /* tail kept right */
    CHECK(ser_is(top, "<r a=\"3\" c=\"4\"/>"));
    CHECK(turdom_remove_attr(r, "a") == 1);       /* the head */
    CHECK(turdom_attr_count(r) == 1);
    CHECK(turdom_set_attr(r, "x:k", "urn:x", "k", "5", 1) == TURDOM_OK);
    CHECK(turdom_set_attr(r, "y:k", "urn:x", "k", "6", 1) == TURDOM_OK);  /* same ns+local */
    CHECK(turdom_attr_count(r) == 2);
    CHECK(strcmp(turdom_attr_get(r, "{urn:x}k"), "6") == 0);
    /* text content */
    turdom_node *t = text(d, "abc");
    turdom_append(r, t);
    turdom_append(r, el(d, "e"));
    CHECK(turdom_set_value(t, "xyz", 3) == TURDOM_OK);
    CHECK(strcmp(turdom_value(t), "xyz") == 0);
    CHECK(turdom_set_text(r, "new", 3) == TURDOM_OK);
    CHECK(turdom_child_count(r) == 1 && turdom_parent(t) == NULL);
    CHECK(ser_is(r, "<r c=\"4\" x:k=\"6\">new</r>"));
    CHECK(turdom_set_text(r, "", 0) == TURDOM_OK);
    CHECK(turdom_child_count(r) == 0);
    /* rename keeps attributes and children */
    turdom_append(r, text(d, "k"));
    int64_t g = turdom_garbage_bytes(d);
    CHECK(turdom_rename(r, "s", NULL, NULL) == TURDOM_OK);
    CHECK(turdom_garbage_bytes(d) > g);
    CHECK(ser_is(top, "<s c=\"4\" x:k=\"6\">k</s>"));
    turdom_node *pi = turdom_create_pi(d, "go", "now");
    CHECK(turdom_append(top, pi) == TURDOM_OK);
    CHECK(turdom_set_value(pi, "later", 5) == TURDOM_OK);
    CHECK(ser_is(top, "<s c=\"4\" x:k=\"6\">k</s><?go later?>"));
    CHECK(links_ok(top));
    turdom_doc_free(d);
}

static void test_html_names(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_HTML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *html = turdom_create_element(d, "HTML", H, NULL);
    turdom_node *body = el(d, "Body");
    turdom_node *svg = turdom_create_element(d, "svg", SVG, NULL);
    turdom_node *fo = turdom_create_element(d, "foreignObject", SVG, NULL);
    turdom_append(top, html);
    turdom_append(html, body);
    turdom_append(body, svg);
    turdom_append(svg, fo);
    CHECK(strcmp(turdom_name(html), "html") == 0);
    CHECK(strcmp(turdom_name(body), "body") == 0);
    CHECK(strcmp(turdom_name(fo), "foreignObject") == 0);   /* not HTML: kept */
    CHECK(turdom_set_attr(body, "CLASS", NULL, NULL, "a", 1) == TURDOM_OK);
    CHECK(turdom_set_attr(body, "class", NULL, NULL, "b", 1) == TURDOM_OK);
    CHECK(turdom_attr_count(body) == 1);
    CHECK(turdom_set_attr(fo, "viewBox", NULL, NULL, "0", 1) == TURDOM_OK);
    CHECK(ser_is(top, "<html><body class=\"b\"><svg><foreignObject viewBox=\"0\"></foreignObject></svg></body></html>"));
    CHECK(turdom_remove_attr(body, "Class") == 1);
    CHECK(links_ok(top));
    turdom_doc_free(d);
}

static void test_copies(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_doc *o = turdom_doc_new(TURDOM_FLAVOR_HTML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = turdom_create_element(d, "x:r", "urn:x", "r");
    turdom_append(top, r);
    turdom_set_attr(r, "id", NULL, NULL, "1", 1);
    turdom_node *a = el(d, "a");
    turdom_append(r, a);
    turdom_append(a, text(d, "t"));
    turdom_append(r, turdom_create_chars(d, TURDOM_COMMENT, " c ", 3));
    turdom_append(r, turdom_create_chars(d, TURDOM_CDATA, "<&>", 3));
    turdom_node *c = turdom_clone(r);
    CHECK(c && turdom_parent(c) == NULL && turdom_node_doc(c) == d);
    CHECK(turdom_equal(c, r));
    CHECK(strcmp(turdom_ns_uri(c), "urn:x") == 0 && strcmp(turdom_local_name(c), "r") == 0);
    turdom_set_attr(c, "id", NULL, NULL, "2", 1);
    CHECK(!turdom_equal(c, r));
    CHECK(strcmp(turdom_attr_get(r, "id"), "1") == 0);
    turdom_node *leaf = turdom_clone(turdom_first_child(a));
    CHECK(leaf && turdom_kind(leaf) == TURDOM_TEXT && strcmp(turdom_value(leaf), "t") == 0);
    turdom_node *i = turdom_import(o, a);
    CHECK(i && turdom_node_doc(i) == o && turdom_equal(i, a));
    CHECK(turdom_append(r, i) == TURDOM_E_WRONG_DOC);
    turdom_node *ohtml = el(o, "html");
    CHECK(turdom_append(turdom_doc_node(o), ohtml) == TURDOM_OK);
    CHECK(turdom_append(ohtml, i) == TURDOM_OK);
    CHECK(ser_is(turdom_doc_node(o), "<html><a>t</a></html>"));
    CHECK(links_ok(top) && links_ok(c) && links_ok(turdom_doc_node(o)));
    turdom_doc_free(o);
    turdom_doc_free(d);
}

static void test_normalize(void) {
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r"), *b = el(d, "b");
    turdom_append(top, r);
    turdom_append(r, text(d, ""));
    turdom_append(r, text(d, "a"));
    turdom_append(r, text(d, "b"));
    turdom_append(r, text(d, ""));
    turdom_append(r, b);
    turdom_append(b, text(d, ""));
    turdom_append(b, text(d, ""));
    turdom_append(r, text(d, "c"));
    turdom_append(r, turdom_create_chars(d, TURDOM_CDATA, "d", 1));
    turdom_append(r, text(d, "e"));
    turdom_append(r, text(d, "f"));
    turdom_normalize(top);
    CHECK(turdom_child_count(r) == 5);
    CHECK(turdom_child_count(b) == 0);
    CHECK(ser_is(top, "<r>ab<b/>c<![CDATA[d]]>ef</r>"));
    CHECK(links_ok(top));
    turdom_doc_free(d);
}

static void test_compact_and_bytes(void) {
    int64_t base = turdom_live_bytes();
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *top = turdom_doc_node(d);
    turdom_node *r = el(d, "r");
    turdom_append(top, r);
    char big[4096];
    memset(big, 'x', sizeof big);
    for (int k = 0; k < 64; k++) {
        turdom_set_attr(r, "a", NULL, NULL, big, sizeof big);    /* 64 replaced values */
        turdom_node *t = turdom_create_chars(d, TURDOM_TEXT, big, sizeof big);
        turdom_append(r, t);
        turdom_remove(t);                                        /* detached, not garbage */
    }
    turdom_append(r, text(d, "kept"));
    CHECK(turdom_garbage_bytes(d) >= 63 * 4096);
    CHECK(turdom_node_count(d) == 66);
    turdom_doc *c = turdom_compact(d);
    CHECK(c != NULL);
    CHECK(turdom_equal(turdom_doc_node(c), top));
    CHECK(turdom_garbage_bytes(c) == 0);
    CHECK(turdom_node_count(c) == 2);
    CHECK(turdom_doc_flavor(c) == TURDOM_FLAVOR_XML);
    CHECK(turdom_live_bytes() > base);
    turdom_doc_free(d);
    turdom_doc_free(c);
    CHECK(turdom_live_bytes() == base);
}

static void test_deep(void) {
    enum { DEPTH = 100000 };
    int64_t base = turdom_live_bytes();
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *n = turdom_doc_node(d);
    for (int k = 0; k < DEPTH; k++) {
        turdom_node *e = el(d, "e");
        turdom_append(n, e);
        n = e;
        if (k % 3 == 0) {
            turdom_append(n, text(d, "a"));
            turdom_append(n, text(d, "b"));
        }
    }
    turdom_node *root = turdom_root_element(d);
    turdom_node *c = turdom_clone(root);
    CHECK(c && turdom_equal(c, root));
    turdom_doc *o = turdom_doc_new(TURDOM_FLAVOR_XML);
    turdom_node *i = turdom_import(o, root);
    CHECK(i && turdom_append(turdom_doc_node(o), i) == TURDOM_OK);
    turdom_doc *k = turdom_compact(d);
    CHECK(k && turdom_equal(turdom_doc_node(k), turdom_doc_node(d)));
    turdom_normalize(turdom_doc_node(d));
    CHECK(turdom_depth(n) == DEPTH);
    CHECK(!turdom_equal(turdom_doc_node(k), turdom_doc_node(d)));   /* texts merged */
    CHECK(links_ok(turdom_doc_node(d)));
    turdom_doc_free(k);
    turdom_doc_free(o);
    turdom_doc_free(d);
    CHECK(turdom_live_bytes() == base);
}

int main(void) {
    test_build_and_order();
    test_errors();
    test_remove_replace_unwrap();
    test_content();
    test_html_names();
    test_copies();
    test_normalize();
    test_compact_and_bytes();
    test_deep();
    CHECK(turdom_live_bytes() == 0);
    if (g_fails) {
        printf("# %d of %d checks failed\n", g_fails, g_checks);
        return 1;
    }
    printf("ok - turdom edit: %d checks\n", g_checks);
    return 0;
}
