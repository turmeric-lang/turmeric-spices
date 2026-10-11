/* c/html/turhtml.c -- lexbor glue for tur-html: tree conversion and a
 * tokenizer-level event queue.
 *
 * DOM: lexbor parses the input with the full WHATWG tree-construction
 * algorithm (tag soup, implied tags, foster parenting, foreign content), and
 * the resulting lexbor tree is copied into a dom-core turdom_doc, so HTML and
 * XML documents share one Node API.  The copy is an explicit-stack walk --
 * nothing here recurses on tree depth -- and it applies the depth cap the way
 * Blink does: an element nested deeper than max_depth is attached beside its
 * parent instead of under it, so the tree stays at max_depth levels and no
 * content is dropped.  <template> contents (a separate document fragment in
 * the DOM) are copied in as the template element's children.
 *
 * SAX: lexbor's tokenizer alone, without the tree builder.  The callbacks
 * record tokens into a queue the Turmeric driver drains, exactly as in
 * tur-xml, with text coalesced across tokens and chunks.  The tokenizer needs
 * the tree builder to switch it into RCDATA / RAWTEXT / script-data /
 * PLAINTEXT after <title>, <style>, <script> and friends; the token callback
 * does that switch itself, by tag name, so the contents of those elements
 * come out as text rather than as tags.
 */

#include <lexbor/html/html.h>
#include <lexbor/html/tokenizer.h>
#include <lexbor/html/tokenizer/state.h>
#include <lexbor/html/tokenizer/state_rawtext.h>
#include <lexbor/html/tokenizer/state_rcdata.h>
#include <lexbor/html/tokenizer/state_script.h>
#include <lexbor/html/interfaces/template_element.h>
#include <lexbor/dom/interfaces/document_type.h>
#include <lexbor/dom/interfaces/processing_instruction.h>

#include <errno.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the dom-core tree (c/dom/turdom.c in tur-dom-core) ----------------
 * Declared here rather than included: one spice's :c-includes are not
 * visible to another.  Keep in step with turdom.h. */
typedef struct turdom_doc turdom_doc;
typedef struct turdom_node turdom_node;
extern turdom_doc  *turdom_doc_new(int flavor);
extern void         turdom_doc_free(turdom_doc *d);
extern turdom_node *turdom_doc_node(turdom_doc *d);
extern turdom_node *turdom_append_element(turdom_doc *, turdom_node *,
                                          const char *, const char *,
                                          const char *, int64_t, int64_t);
extern void         turdom_add_attr(turdom_doc *, turdom_node *, const char *,
                                    const char *, const char *, const char *,
                                    size_t);
extern turdom_node *turdom_append_chars(turdom_doc *, turdom_node *, int,
                                        const char *, size_t, int64_t, int64_t);
extern turdom_node *turdom_append_pi(turdom_doc *, turdom_node *, const char *,
                                     const char *, int64_t, int64_t);
extern turdom_node *turdom_append_doctype(turdom_doc *, turdom_node *,
                                          const char *, const char *,
                                          const char *, int64_t, int64_t);
#define TURDOM_TEXT    2
#define TURDOM_COMMENT 3
#define TURDOM_CDATA   4
#define TURDOM_FLAVOR_HTML 1

/* ParseErrorKind, in dom/core's constructor order. */
enum { ERR_SYNTAX = 0, ERR_LIMIT = 1, ERR_REFUSED = 2, ERR_IO = 3 };

/* ---- shared ------------------------------------------------------------- */

static const char *ns_uri(uintptr_t ns) {
    switch (ns) {
    case LXB_NS_HTML:  return "http://www.w3.org/1999/xhtml";
    case LXB_NS_SVG:   return "http://www.w3.org/2000/svg";
    case LXB_NS_MATH:  return "http://www.w3.org/1998/Math/MathML";
    case LXB_NS_XLINK: return "http://www.w3.org/1999/xlink";
    case LXB_NS_XML:   return "http://www.w3.org/XML/1998/namespace";
    case LXB_NS_XMLNS: return "http://www.w3.org/2000/xmlns/";
    default:           return NULL;
    }
}

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n && s) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* A copy of [s, s+n) with every NUL replaced by U+FFFD, as the HTML spec
 * does for character data; the result is safe as a C string. */
static char *dup_text(const char *s, size_t n, size_t *out_len) {
    size_t nuls = 0;
    for (size_t i = 0; i < n; i++) if (s[i] == '\0') nuls++;
    char *p = (char *)malloc(n + nuls * 2 + 1);
    if (!p) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0') { p[o++] = (char)0xEF; p[o++] = (char)0xBF; p[o++] = (char)0xBD; }
        else p[o++] = s[i];
    }
    p[o] = '\0';
    if (out_len) *out_len = o;
    return p;
}

/* Error state for the DOM entry points (there is no parser object to hang it
 * on).  Per thread so concurrent parses do not trample each other. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#define TURHTML_TLS _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define TURHTML_TLS __thread
#else
#define TURHTML_TLS
#endif
static TURHTML_TLS int g_err_kind = ERR_SYNTAX;
static TURHTML_TLS char g_err_msg[512];

static void set_err(int kind, const char *msg) {
    g_err_kind = kind;
    snprintf(g_err_msg, sizeof g_err_msg, "%s", msg ? msg : "parse error");
}

int turhtml_err_kind(void) { return g_err_kind; }
const char *turhtml_err_msg(void) { return g_err_msg; }

/* ---- DOM: copy a lexbor tree into a turdom_doc --------------------------- */

typedef struct {
    lxb_dom_node_t *n;
    turdom_node *parent;
    int64_t depth;
} walk_item;

typedef struct {
    walk_item *v;
    size_t len, cap;
} walk_stack;

static int ws_push(walk_stack *s, lxb_dom_node_t *n, turdom_node *p, int64_t d) {
    if (s->len == s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 256;
        walk_item *nv = (walk_item *)realloc(s->v, cap * sizeof(walk_item));
        if (!nv) return 0;
        s->v = nv;
        s->cap = cap;
    }
    s->v[s->len].n = n;
    s->v[s->len].parent = p;
    s->v[s->len].depth = d;
    s->len++;
    return 1;
}

/* The children to descend into: a <template>'s are in its content fragment. */
static lxb_dom_node_t *last_child_of(lxb_dom_node_t *n) {
    if (n->type == LXB_DOM_NODE_TYPE_ELEMENT && n->ns == LXB_NS_HTML &&
        n->local_name == LXB_TAG_TEMPLATE) {
        lxb_html_template_element_t *t = lxb_html_interface_template(n);
        if (t->content) return lxb_dom_interface_node(t->content)->last_child;
    }
    return n->last_child;
}

static int push_children(walk_stack *s, lxb_dom_node_t *n, turdom_node *p,
                         int64_t d) {
    for (lxb_dom_node_t *c = last_child_of(n); c; c = c->prev)
        if (!ws_push(s, c, p, d)) return 0;
    return 1;
}

/* Local names.  lexbor lower-cases its local_name ids, but the DOM local
 * name of a foreign element or attribute is the case-adjusted one
 * (foreignObject, viewBox, definitionURL), which only lexbor's qualified
 * name preserves -- so names are taken from the qualified name.  The HTML
 * parser never gives an element a prefix (`<xyz:abc>` is an element whose
 * local name is "xyz:abc"), and of the attributes only the adjusted
 * xlink:, xml: and xmlns: ones have one; for those the local name is what
 * follows the colon. */
static const char *local_part(const char *q) {
    const char *c = strchr(q, ':');
    return c ? c + 1 : q;
}

/* An attribute's namespace.  lexbor tags an unprefixed attribute of an SVG
 * or MathML element with the element's namespace id; in the DOM such an
 * attribute is in no namespace, and only the xlink:, xml: and xmlns
 * attributes the tree builder adjusts carry one. */
static const char *attr_ns_uri(uintptr_t ns) {
    switch (ns) {
    case LXB_NS_XLINK: case LXB_NS_XML: case LXB_NS_XMLNS: return ns_uri(ns);
    default: return NULL;
    }
}

static turdom_node *copy_element(turdom_doc *d, turdom_node *parent,
                                 lxb_dom_node_t *n) {
    lxb_dom_element_t *el = lxb_dom_interface_element(n);
    size_t qlen = 0;
    const lxb_char_t *q = lxb_dom_element_qualified_name(el, &qlen);
    char *qs = xstrndup((const char *)q, q ? qlen : 0);
    turdom_node *out = NULL;
    if (qs)
        out = turdom_append_element(d, parent, qs, ns_uri(n->ns), qs, 0, 0);
    free(qs);
    if (!out) return NULL;
    for (lxb_dom_attr_t *a = lxb_dom_element_first_attribute(el); a;
         a = lxb_dom_element_next_attribute(a)) {
        size_t nlen = 0, vlen = 0;
        const lxb_char_t *an = lxb_dom_attr_qualified_name(a, &nlen);
        const lxb_char_t *av = lxb_dom_attr_value(a, &vlen);
        char *aq = xstrndup((const char *)an, an ? nlen : 0);
        const char *ans = attr_ns_uri(a->node.ns);
        if (aq)
            turdom_add_attr(d, out, aq, ans, ans ? local_part(aq) : aq,
                            av ? (const char *)av : "", av ? vlen : 0);
        free(aq);
    }
    return out;
}

static int copy_leaf(turdom_doc *d, turdom_node *parent, lxb_dom_node_t *n) {
    switch (n->type) {
    case LXB_DOM_NODE_TYPE_TEXT:
    case LXB_DOM_NODE_TYPE_COMMENT:
    case LXB_DOM_NODE_TYPE_CDATA_SECTION: {
        lxb_dom_character_data_t *cd = lxb_dom_interface_character_data(n);
        size_t len = 0;
        char *t = dup_text((const char *)cd->data.data,
                           cd->data.data ? cd->data.length : 0, &len);
        if (!t) return 0;
        int kind = n->type == LXB_DOM_NODE_TYPE_TEXT ? TURDOM_TEXT
                 : n->type == LXB_DOM_NODE_TYPE_COMMENT ? TURDOM_COMMENT
                 : TURDOM_CDATA;
        turdom_node *r = turdom_append_chars(d, parent, kind, t, len, 0, 0);
        free(t);
        return r != NULL;
    }
    case LXB_DOM_NODE_TYPE_PROCESSING_INSTRUCTION: {
        lxb_dom_processing_instruction_t *pi =
            lxb_dom_interface_processing_instruction(n);
        size_t tl = 0;
        const lxb_char_t *tg = lxb_dom_processing_instruction_target(pi, &tl);
        char *target = xstrndup((const char *)tg, tg ? tl : 0);
        size_t dl = 0;
        char *data = dup_text((const char *)pi->char_data.data.data,
                              pi->char_data.data.data ? pi->char_data.data.length : 0,
                              &dl);
        turdom_node *r = (target && data)
                         ? turdom_append_pi(d, parent, target, data, 0, 0) : NULL;
        free(target);
        free(data);
        return r != NULL;
    }
    case LXB_DOM_NODE_TYPE_DOCUMENT_TYPE: {
        lxb_dom_document_type_t *dt = lxb_dom_interface_document_type(n);
        size_t nl = 0, pl = 0, sl = 0;
        const lxb_char_t *nm = lxb_dom_document_type_name(dt, &nl);
        const lxb_char_t *pb = lxb_dom_document_type_public_id(dt, &pl);
        const lxb_char_t *sy = lxb_dom_document_type_system_id(dt, &sl);
        char *name = xstrndup((const char *)nm, nm ? nl : 0);
        char *pub = (pb && pl) ? xstrndup((const char *)pb, pl) : NULL;
        char *sys = (sy && sl) ? xstrndup((const char *)sy, sl) : NULL;
        turdom_node *r = name ? turdom_append_doctype(d, parent, name, pub, sys, 0, 0)
                              : NULL;
        free(name);
        free(pub);
        free(sys);
        return r != NULL;
    }
    default:
        return 1;               /* nothing else occurs in a parsed tree */
    }
}

/* Copy every child of `from` (in lexbor) under `to` (in d). */
static int copy_tree(turdom_doc *d, turdom_node *to, lxb_dom_node_t *from,
                     int64_t max_depth) {
    walk_stack s = {0};
    int ok = push_children(&s, from, to, 1);
    while (ok && s.len > 0) {
        walk_item it = s.v[--s.len];
        if (it.n->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            turdom_node *el = copy_element(d, it.parent, it.n);
            if (!el) { ok = 0; break; }
            /* At the cap, children go beside this element, not under it. */
            if (max_depth <= 0 || it.depth < max_depth)
                ok = push_children(&s, it.n, el, it.depth + 1);
            else
                ok = push_children(&s, it.n, it.parent, it.depth);
        } else {
            ok = copy_leaf(d, it.parent, it.n);
        }
    }
    free(s.v);
    return ok;
}

/* Parse a whole document.  Returns a turdom_doc, or NULL (error recorded). */
void *turhtml_parse(const char *src, int64_t len, int64_t max_depth,
                    int64_t max_bytes) {
    if (len < 0) len = (int64_t)strlen(src);
    if (max_bytes > 0 && len > max_bytes) {
        char msg[96];
        snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                 (long long)max_bytes);
        set_err(ERR_LIMIT, msg);
        return NULL;
    }
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) { set_err(ERR_LIMIT, "out of memory"); return NULL; }
    lxb_dom_document_opt_set(lxb_dom_interface_document(doc),
                             LXB_DOM_DOCUMENT_OPT_WO_EVENTS);
    if (lxb_html_document_parse(doc, (const lxb_char_t *)src, (size_t)len) !=
        LXB_STATUS_OK) {
        lxb_html_document_destroy(doc);
        set_err(ERR_LIMIT, "out of memory");
        return NULL;
    }
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_HTML);
    if (!d || !copy_tree(d, turdom_doc_node(d), lxb_dom_interface_node(doc),
                         max_depth)) {
        turdom_doc_free(d);
        lxb_html_document_destroy(doc);
        set_err(ERR_LIMIT, "out of memory");
        return NULL;
    }
    lxb_html_document_destroy(doc);
    return d;
}

/* Parse a fragment as if it were the innerHTML of a `context` element (the
 * HTML fragment parsing algorithm).  The fragment's top-level nodes become
 * the document node's children. */
void *turhtml_parse_fragment(const char *context, const char *src, int64_t len,
                             int64_t max_depth, int64_t max_bytes) {
    if (len < 0) len = (int64_t)strlen(src);
    if (max_bytes > 0 && len > max_bytes) {
        char msg[96];
        snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                 (long long)max_bytes);
        set_err(ERR_LIMIT, msg);
        return NULL;
    }
    lxb_html_document_t *doc = lxb_html_document_create();
    if (!doc) { set_err(ERR_LIMIT, "out of memory"); return NULL; }
    lxb_dom_document_opt_set(lxb_dom_interface_document(doc),
                             LXB_DOM_DOCUMENT_OPT_WO_EVENTS);
    const char *ctx = (context && context[0]) ? context : "body";
    lxb_dom_element_t *el = lxb_dom_document_create_element(
        lxb_dom_interface_document(doc), (const lxb_char_t *)ctx, strlen(ctx),
        NULL);
    lxb_dom_node_t *root = el ? lxb_html_document_parse_fragment(
                                    doc, el, (const lxb_char_t *)src, (size_t)len)
                              : NULL;
    if (!root) {
        lxb_html_document_destroy(doc);
        set_err(ERR_LIMIT, "out of memory");
        return NULL;
    }
    turdom_doc *d = turdom_doc_new(TURDOM_FLAVOR_HTML);
    if (!d || !copy_tree(d, turdom_doc_node(d), root, max_depth)) {
        turdom_doc_free(d);
        lxb_html_document_destroy(doc);
        set_err(ERR_LIMIT, "out of memory");
        return NULL;
    }
    lxb_html_document_destroy(doc);
    return d;
}

/* Read a whole file (HTML parsing needs it in one piece only because the
 * copy happens after the parse; lexbor itself could take chunks). */
void *turhtml_parse_file(const char *path, int64_t max_depth, int64_t max_bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        char msg[512];
        snprintf(msg, sizeof msg, "cannot open '%s': %s", path, strerror(errno));
        set_err(ERR_IO, msg);
        return NULL;
    }
    size_t cap = 65536, len = 0;
    char *buf = (char *)malloc(cap);
    for (;;) {
        if (!buf) { fclose(f); set_err(ERR_LIMIT, "out of memory"); return NULL; }
        size_t n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (max_bytes > 0 && (int64_t)len > max_bytes) {
            free(buf);
            fclose(f);
            char msg[96];
            snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                     (long long)max_bytes);
            set_err(ERR_LIMIT, msg);
            return NULL;
        }
        if (len < cap) break;
        cap *= 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) free(buf);
        buf = nb;
    }
    int err = ferror(f);
    fclose(f);
    if (err) { free(buf); set_err(ERR_IO, "read error"); return NULL; }
    void *d = turhtml_parse(buf, (int64_t)len, max_depth, 0);
    free(buf);
    return d;
}

/* ---- SAX: the tokenizer and an event queue -------------------------------
 * Event kinds match tur-xml's (src/html/sax.tur decodes them the same way). */

enum { EV_START = 1, EV_END = 2, EV_CHARS = 3, EV_COMMENT = 4, EV_DOCTYPE = 8 };

#define TURHTML_CHUNK ((size_t)64 * 1024)

typedef struct {
    int kind;
    char *name;
    char *text;
    char *pubid;
    char *sysid;
    int nattrs;
    char **attr_names;
    char **attr_values;
} hev;

typedef struct turhtml {
    lxb_html_tokenizer_t *tkz;
    int64_t max_bytes;
    int64_t bytes;
    hev *q;
    size_t qhead, qlen, qcap;
    hev cur;
    int has_cur;
    char *text;
    size_t tlen, tcap;
    int has_text;
    char *pend;             /* held-back tail; see hold_back() */
    size_t pend_len, pend_cap;
    const char *src;
    size_t src_len, src_off;
    int src_set;
    int finished;
    int stopped;
    int failed;
    int err_kind;
    char *err_msg;
} turhtml;

static void hev_free(hev *e) {
    free(e->name);
    free(e->text);
    free(e->pubid);
    free(e->sysid);
    for (int i = 0; i < e->nattrs; i++) {
        free(e->attr_names[i]);
        free(e->attr_values[i]);
    }
    free(e->attr_names);
    free(e->attr_values);
    memset(e, 0, sizeof(*e));
}

static void h_fail(turhtml *x, int kind, const char *msg) {
    if (x->failed) return;
    x->failed = 1;
    x->err_kind = kind;
    x->err_msg = xstrndup(msg, strlen(msg));
}

static hev *h_push(turhtml *x, int kind) {
    if (x->qhead + x->qlen == x->qcap) {
        if (x->qhead > 0) {
            memmove(x->q, x->q + x->qhead, x->qlen * sizeof(hev));
            x->qhead = 0;
        } else {
            size_t cap = x->qcap ? x->qcap * 2 : 64;
            hev *nq = (hev *)realloc(x->q, cap * sizeof(hev));
            if (!nq) { h_fail(x, ERR_LIMIT, "out of memory"); return NULL; }
            x->q = nq;
            x->qcap = cap;
        }
    }
    hev *e = &x->q[x->qhead + x->qlen++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    return e;
}

static void h_flush_text(turhtml *x) {
    if (!x->has_text || x->failed) return;
    hev *e = h_push(x, EV_CHARS);
    if (!e) return;
    e->text = xstrndup(x->text ? x->text : "", x->tlen);
    if (!e->text) h_fail(x, ERR_LIMIT, "out of memory");
    x->tlen = 0;
    x->has_text = 0;
}

static void h_append_text(turhtml *x, const char *s, size_t n) {
    if (n == 0) return;
    size_t need = x->tlen + n * 3 + 1;  /* room for NUL -> U+FFFD */
    if (need > x->tcap) {
        size_t cap = x->tcap ? x->tcap : 256;
        while (cap < need) cap *= 2;
        char *nt = (char *)realloc(x->text, cap);
        if (!nt) { h_fail(x, ERR_LIMIT, "out of memory"); return; }
        x->text = nt;
        x->tcap = cap;
    }
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\0') {
            x->text[x->tlen++] = (char)0xEF;
            x->text[x->tlen++] = (char)0xBF;
            x->text[x->tlen++] = (char)0xBD;
        } else {
            x->text[x->tlen++] = s[i];
        }
    }
    x->has_text = 1;
}

static char *tag_name(lxb_tag_id_t id) {
    size_t len = 0;
    const lxb_char_t *nm = lxb_tag_name_by_id(id, &len);
    return xstrndup(nm ? (const char *)nm : "", nm ? len : 0);
}

static void switch_state(lxb_html_tokenizer_t *tkz, lxb_tag_id_t id) {
    switch (id) {
    case LXB_TAG_TITLE:
    case LXB_TAG_TEXTAREA:
        lxb_html_tokenizer_tmp_tag_id_set(tkz, id);
        lxb_html_tokenizer_state_set(tkz, lxb_html_tokenizer_state_rcdata_before);
        break;
    case LXB_TAG_STYLE:
    case LXB_TAG_XMP:
    case LXB_TAG_IFRAME:
    case LXB_TAG_NOEMBED:
    case LXB_TAG_NOFRAMES:
        lxb_html_tokenizer_tmp_tag_id_set(tkz, id);
        lxb_html_tokenizer_state_set(tkz, lxb_html_tokenizer_state_rawtext_before);
        break;
    case LXB_TAG_SCRIPT:
        lxb_html_tokenizer_tmp_tag_id_set(tkz, id);
        lxb_html_tokenizer_state_set(tkz,
                                     lxb_html_tokenizer_state_script_data_before);
        break;
    case LXB_TAG_PLAINTEXT:
        lxb_html_tokenizer_state_set(tkz,
                                     lxb_html_tokenizer_state_plaintext_before);
        break;
    default:
        break;
    }
}

static void doctype_event(turhtml *x, lxb_html_token_t *token) {
    h_flush_text(x);
    hev *e = h_push(x, EV_DOCTYPE);
    if (!e) return;
    lxb_html_token_attr_t *a = token->attr_first;
    size_t nl = 0;
    const lxb_char_t *nm = a ? lxb_html_token_attr_name(a, &nl) : NULL;
    e->name = xstrndup(nm ? (const char *)nm : "", nm ? nl : 0);
    if (a && a->next) {
        lxb_html_token_attr_t *k = a->next;
        size_t kl = 0;
        const lxb_char_t *kn = lxb_html_token_attr_name(k, &kl);
        int is_public = kn && kl == 6 && strncasecmp((const char *)kn, "public", 6) == 0;
        int is_system = kn && kl == 6 && strncasecmp((const char *)kn, "system", 6) == 0;
        if (is_public) {
            e->pubid = xstrndup((const char *)k->value, k->value ? k->value_size : 0);
            if (k->next)
                e->sysid = xstrndup((const char *)k->next->value,
                                    k->next->value ? k->next->value_size : 0);
        } else if (is_system) {
            e->sysid = xstrndup((const char *)k->value, k->value ? k->value_size : 0);
        }
    }
    if (!e->name) h_fail(x, ERR_LIMIT, "out of memory");
}

static lxb_html_token_t *on_token(lxb_html_tokenizer_t *tkz,
                                  lxb_html_token_t *token, void *ctx) {
    turhtml *x = (turhtml *)ctx;
    if (x->failed || x->stopped) return token;
    lxb_tag_id_t id = token->tag_id;
    if (id == LXB_TAG__TEXT) {
        h_append_text(x, (const char *)token->text_start,
                      (size_t)(token->text_end - token->text_start));
        return token;
    }
    if (id == LXB_TAG__END_OF_FILE) return token;
    if (id == LXB_TAG__EM_DOCTYPE) { doctype_event(x, token); return token; }
    h_flush_text(x);
    if (id == LXB_TAG__EM_COMMENT) {
        hev *e = h_push(x, EV_COMMENT);
        if (!e) return token;
        e->text = dup_text((const char *)token->text_start,
                           (size_t)(token->text_end - token->text_start), NULL);
        if (!e->text) h_fail(x, ERR_LIMIT, "out of memory");
        return token;
    }
    if (token->type & LXB_HTML_TOKEN_TYPE_CLOSE) {
        hev *e = h_push(x, EV_END);
        if (!e) return token;
        e->name = tag_name(id);
        if (!e->name) h_fail(x, ERR_LIMIT, "out of memory");
        return token;
    }
    hev *e = h_push(x, EV_START);
    if (!e) return token;
    e->name = tag_name(id);
    int n = 0;
    for (lxb_html_token_attr_t *a = token->attr_first; a; a = a->next) n++;
    if (n) {
        e->attr_names = (char **)calloc((size_t)n, sizeof(char *));
        e->attr_values = (char **)calloc((size_t)n, sizeof(char *));
        if (!e->attr_names || !e->attr_values) {
            h_fail(x, ERR_LIMIT, "out of memory");
            return token;
        }
        e->nattrs = n;
        int i = 0;
        for (lxb_html_token_attr_t *a = token->attr_first; a; a = a->next, i++) {
            size_t nl = 0;
            const lxb_char_t *an = lxb_html_token_attr_name(a, &nl);
            e->attr_names[i] = xstrndup(an ? (const char *)an : "", an ? nl : 0);
            e->attr_values[i] = dup_text(a->value ? (const char *)a->value : "",
                                         a->value ? a->value_size : 0, NULL);
        }
    }
    if (token->type & LXB_HTML_TOKEN_TYPE_CLOSE_SELF) {
        hev *c = h_push(x, EV_END);
        if (c) c->name = tag_name(id);
    } else {
        switch_state(tkz, id);
    }
    return token;
}

void *turhtml_sax_new(int64_t max_bytes) {
    turhtml *x = (turhtml *)calloc(1, sizeof(turhtml));
    if (!x) return NULL;
    x->max_bytes = max_bytes;
    x->tkz = lxb_html_tokenizer_create();
    if (!x->tkz || lxb_html_tokenizer_init(x->tkz) != LXB_STATUS_OK) {
        if (x->tkz) lxb_html_tokenizer_destroy(x->tkz);
        free(x);
        return NULL;
    }
    lxb_html_tokenizer_callback_token_done_set(x->tkz, on_token, x);
    if (lxb_html_tokenizer_begin(x->tkz) != LXB_STATUS_OK) {
        lxb_html_tokenizer_destroy(x->tkz);
        free(x);
        return NULL;
    }
    return x;
}

void turhtml_sax_free(void *xp) {
    turhtml *x = (turhtml *)xp;
    if (!x) return;
    if (x->tkz) lxb_html_tokenizer_destroy(x->tkz);
    for (size_t i = 0; i < x->qlen; i++) hev_free(&x->q[x->qhead + i]);
    free(x->q);
    if (x->has_cur) hev_free(&x->cur);
    free(x->text);
    free(x->pend);
    free(x->err_msg);
    free(x);
}

/* lexbor 3.0.1's tokenizer mishandles some lookaheads that straddle a chunk
 * boundary: a markup declaration's "[CDATA[" (the bogus comment it produces
 * repeats characters -- "[CDATAA[" -- or upper-cases a "[cdata[" that was
 * not a match), and a DOCTYPE's PUBLIC / SYSTEM keyword.  Every construct
 * that looks ahead starts with '<', so the push path never lets lexbor see a
 * boundary inside one: it feeds up to the first '<' that no '>' has closed
 * yet and holds that tail back for the next chunk.  The tail is bounded by
 * the distance to the next '>', which in real markup is short; past
 * TURHTML_MAX_HOLD bytes (a '<' in a very long run of text) it is fed
 * anyway.  The DOM entry points parse in one buffer and are unaffected. */
#define TURHTML_MAX_HOLD ((size_t)1024 * 1024)

static size_t hold_back(const char *s, size_t n) {
    /* The first '<' after the last '>': everything from there on may still
     * be an unfinished construct (and "<![CDA<tem" is two of them). */
    size_t i = n;
    while (i > 0 && s[i - 1] != '>') i--;
    const char *lt = memchr(s + i, '<', n - i);
    if (!lt) return 0;
    size_t keep = (size_t)(s + n - lt);
    return keep > TURHTML_MAX_HOLD ? 0 : keep;
}

static int tokenize(turhtml *x, const char *buf, size_t len) {
    if (len == 0) return 0;
    if (lxb_html_tokenizer_chunk(x->tkz, (const lxb_char_t *)buf, len) !=
        LXB_STATUS_OK) {
        h_fail(x, ERR_LIMIT, "out of memory");
        return -1;
    }
    return 0;
}

/* Feed one chunk (0 ok, -1 failed). */
int turhtml_sax_feed(void *xp, const char *buf, int64_t len, int is_final) {
    turhtml *x = (turhtml *)xp;
    if (!x || x->failed) return -1;
    if (x->finished || x->stopped) return 0;
    if (len < 0) len = 0;
    x->bytes += len;
    if (x->max_bytes > 0 && x->bytes > x->max_bytes) {
        char msg[96];
        snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                 (long long)x->max_bytes);
        h_fail(x, ERR_LIMIT, msg);
        return -1;
    }
    /* Join any held-back tail to the front of this chunk. */
    size_t total = x->pend_len + (size_t)len;
    char *joined = NULL;
    const char *data = buf ? buf : "";
    if (x->pend_len) {
        joined = (char *)malloc(total ? total : 1);
        if (!joined) { h_fail(x, ERR_LIMIT, "out of memory"); return -1; }
        memcpy(joined, x->pend, x->pend_len);
        if (len) memcpy(joined + x->pend_len, buf, (size_t)len);
        data = joined;
        x->pend_len = 0;
    }
    size_t keep = is_final ? 0 : hold_back(data, total);
    int rc = tokenize(x, data, total - keep);
    if (keep && rc == 0) {
        if (keep > x->pend_cap) {
            char *np = (char *)realloc(x->pend, keep);
            if (!np) { free(joined); h_fail(x, ERR_LIMIT, "out of memory"); return -1; }
            x->pend = np;
            x->pend_cap = keep;
        }
        memcpy(x->pend, data + total - keep, keep);
        x->pend_len = keep;
    }
    free(joined);
    if (rc < 0) return -1;
    if (is_final) {
        if (lxb_html_tokenizer_end(x->tkz) != LXB_STATUS_OK) {
            h_fail(x, ERR_LIMIT, "out of memory");
            return -1;
        }
        h_flush_text(x);
        x->finished = 1;
    }
    return x->failed ? -1 : 0;
}

void turhtml_sax_set_string(void *xp, const char *s) {
    turhtml *x = (turhtml *)xp;
    x->src = s ? s : "";
    x->src_len = strlen(x->src);
    x->src_off = 0;
    x->src_set = 1;
}

/* Feed the next chunk of the configured string: 1 more, 0 done, -1 failed. */
int turhtml_sax_pump(void *xp) {
    turhtml *x = (turhtml *)xp;
    if (!x || x->failed) return -1;
    if (x->finished || x->stopped || !x->src_set) return 0;
    size_t n = x->src_len - x->src_off;
    if (n > TURHTML_CHUNK) n = TURHTML_CHUNK;
    int final = x->src_off + n == x->src_len;
    int rc = turhtml_sax_feed(x, x->src + x->src_off, (int64_t)n, final);
    x->src_off += n;
    if (rc < 0) return -1;
    return final ? 0 : 1;
}

int turhtml_sax_next(void *xp) {
    turhtml *x = (turhtml *)xp;
    if (!x) return 0;
    if (x->has_cur) { hev_free(&x->cur); x->has_cur = 0; }
    if (x->stopped || x->qlen == 0) return 0;
    x->cur = x->q[x->qhead];
    x->qhead++;
    x->qlen--;
    if (x->qlen == 0) x->qhead = 0;
    x->has_cur = 1;
    return x->cur.kind;
}

void turhtml_sax_stop(void *xp) {
    turhtml *x = (turhtml *)xp;
    if (!x) return;
    x->stopped = 1;
    for (size_t i = 0; i < x->qlen; i++) hev_free(&x->q[x->qhead + i]);
    x->qhead = 0;
    x->qlen = 0;
}

int turhtml_sax_err_kind(void *xp) { return xp ? ((turhtml *)xp)->err_kind : ERR_SYNTAX; }
const char *turhtml_sax_err_msg(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->err_msg) ? x->err_msg : "parse error";
}
const char *turhtml_ev_name(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->has_cur && x->cur.name) ? x->cur.name : "";
}
const char *turhtml_ev_text(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->has_cur && x->cur.text) ? x->cur.text : "";
}
const char *turhtml_ev_pubid(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->has_cur && x->cur.pubid) ? x->cur.pubid : "";
}
const char *turhtml_ev_sysid(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->has_cur && x->cur.sysid) ? x->cur.sysid : "";
}
int64_t turhtml_ev_nattrs(void *xp) {
    turhtml *x = (turhtml *)xp;
    return (x && x->has_cur) ? x->cur.nattrs : 0;
}
const char *turhtml_ev_attr_name(void *xp, int64_t i) {
    turhtml *x = (turhtml *)xp;
    if (!x || !x->has_cur || i < 0 || i >= x->cur.nattrs) return "";
    return x->cur.attr_names[i] ? x->cur.attr_names[i] : "";
}
const char *turhtml_ev_attr_value(void *xp, int64_t i) {
    turhtml *x = (turhtml *)xp;
    if (!x || !x->has_cur || i < 0 || i >= x->cur.nattrs) return "";
    return x->cur.attr_values[i] ? x->cur.attr_values[i] : "";
}
