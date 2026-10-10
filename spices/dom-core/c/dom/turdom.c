/* c/dom/turdom.c -- the shared markup tree.  See turdom.h. */

#include "turdom.h"

#include <stdlib.h>
#include <string.h>

/* ---- arena --------------------------------------------------------------
 *
 * Every node, attribute and string of a document lives in one chain of
 * malloc'd chunks, so dom-free is a walk over the chunks rather than over the
 * tree.  Allocations bigger than a chunk get a chunk of their own, linked in
 * behind the current one so the current chunk keeps filling. */

#define TURDOM_CHUNK ((size_t)64 * 1024)

typedef struct arena_chunk {
    struct arena_chunk *next;
    size_t used;
    size_t cap;
    unsigned char data[];
} arena_chunk;

typedef struct turdom_attr {
    const char *name;
    const char *local;
    const char *ns_uri;
    const char *value;
    size_t vlen;
    struct turdom_attr *next;
} turdom_attr;

struct turdom_node {
    int kind;
    turdom_doc *doc;
    const char *name;
    const char *local;
    const char *ns_uri;
    char *value;
    size_t value_len;
    const char *public_id;
    const char *system_id;
    turdom_attr *attrs;
    turdom_attr *attrs_tail;
    int64_t nattrs;
    turdom_node *parent;
    turdom_node *first;
    turdom_node *last;
    turdom_node *next;
    turdom_node *prev;
    int64_t nchildren;
    int64_t line;
    int64_t col;
};

struct turdom_doc {
    int flavor;
    arena_chunk *chunks;
    turdom_node *docnode;
    int64_t count;
};

/* Bytes of arena currently held by every live document in the process.
 * dom-live-bytes reports it, which is how the tests check that dom-free
 * returns everything without needing valgrind on every CI runner. */
static int64_t g_live_bytes = 0;

static void live_add(int64_t n) {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_add_fetch(&g_live_bytes, n, __ATOMIC_RELAXED);
#else
    g_live_bytes += n;
#endif
}

int64_t turdom_live_bytes(void) {
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(&g_live_bytes, __ATOMIC_RELAXED);
#else
    return g_live_bytes;
#endif
}

static void *arena_alloc(turdom_doc *d, size_t n) {
    n = (n + 7u) & ~(size_t)7u;
    if (n == 0) n = 8;
    arena_chunk *c = d->chunks;
    if (c && c->used + n <= c->cap) {
        void *p = c->data + c->used;
        c->used += n;
        return p;
    }
    size_t cap = n > TURDOM_CHUNK ? n : TURDOM_CHUNK;
    arena_chunk *nc = (arena_chunk *)malloc(sizeof(arena_chunk) + cap);
    if (!nc) return NULL;
    nc->cap = cap;
    live_add((int64_t)(sizeof(arena_chunk) + cap));
    if (n > TURDOM_CHUNK && c) {
        /* Oversized: dedicate this chunk and keep filling the current one. */
        nc->used = n;
        nc->next = c->next;
        c->next = nc;
        return nc->data;
    }
    nc->used = n;
    nc->next = c;
    d->chunks = nc;
    return nc->data;
}

static char *arena_strndup(turdom_doc *d, const char *s, size_t n) {
    char *p = (char *)arena_alloc(d, n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static const char *arena_strdup_opt(turdom_doc *d, const char *s) {
    if (!s) return NULL;
    return arena_strndup(d, s, strlen(s));
}

/* ---- growable output buffer ------------------------------------------- */

typedef struct {
    char *p;
    size_t len;
    size_t cap;
    int oom;
} obuf;

static void ob_reserve(obuf *b, size_t extra) {
    if (b->oom) return;
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    char *np = (char *)realloc(b->p, cap);
    if (!np) { b->oom = 1; return; }
    b->p = np;
    b->cap = cap;
}

static void ob_put(obuf *b, const char *s, size_t n) {
    if (!n) return;
    ob_reserve(b, n);
    if (b->oom) return;
    memcpy(b->p + b->len, s, n);
    b->len += n;
}

static void ob_puts(obuf *b, const char *s) { if (s) ob_put(b, s, strlen(s)); }

static char *ob_finish(obuf *b, size_t *len) {
    ob_reserve(b, 0);
    if (b->oom) { free(b->p); if (len) *len = 0; return NULL; }
    b->p[b->len] = '\0';
    if (len) *len = b->len;
    return b->p;
}

/* ---- building ---------------------------------------------------------- */

turdom_doc *turdom_doc_new(int flavor) {
    turdom_doc *d = (turdom_doc *)calloc(1, sizeof(turdom_doc));
    if (!d) return NULL;
    d->flavor = flavor;
    turdom_node *n = (turdom_node *)arena_alloc(d, sizeof(turdom_node));
    if (!n) { free(d); return NULL; }
    memset(n, 0, sizeof(*n));
    n->kind = TURDOM_DOCUMENT;
    n->doc = d;
    d->docnode = n;
    return d;
}

void turdom_doc_free(turdom_doc *d) {
    if (!d) return;
    arena_chunk *c = d->chunks;
    while (c) {
        arena_chunk *nx = c->next;
        live_add(-(int64_t)(sizeof(arena_chunk) + c->cap));
        free(c);
        c = nx;
    }
    free(d);
}

int turdom_doc_flavor(const turdom_doc *d) { return d ? d->flavor : 0; }
turdom_node *turdom_doc_node(turdom_doc *d) { return d ? d->docnode : NULL; }
turdom_doc *turdom_node_doc(const turdom_node *n) { return n ? n->doc : NULL; }
int64_t turdom_node_count(const turdom_doc *d) { return d ? d->count : 0; }

static turdom_node *new_node(turdom_doc *d, turdom_node *parent, int kind,
                             int64_t line, int64_t col) {
    if (!d || !parent) return NULL;
    turdom_node *n = (turdom_node *)arena_alloc(d, sizeof(turdom_node));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->doc = d;
    n->line = line;
    n->col = col;
    n->parent = parent;
    n->prev = parent->last;
    if (parent->last) parent->last->next = n;
    else parent->first = n;
    parent->last = n;
    parent->nchildren++;
    d->count++;
    return n;
}

turdom_node *turdom_append_element(turdom_doc *d, turdom_node *parent,
                                   const char *qname, const char *ns_uri,
                                   const char *local,
                                   int64_t line, int64_t col) {
    turdom_node *n = new_node(d, parent, TURDOM_ELEMENT, line, col);
    if (!n) return NULL;
    n->name = arena_strdup_opt(d, qname ? qname : "");
    n->ns_uri = (ns_uri && ns_uri[0]) ? arena_strdup_opt(d, ns_uri) : NULL;
    n->local = local ? arena_strdup_opt(d, local) : n->name;
    if (!n->name || !n->local) return NULL;
    return n;
}

void turdom_add_attr(turdom_doc *d, turdom_node *el,
                     const char *qname, const char *ns_uri, const char *local,
                     const char *value, size_t vlen) {
    if (!d || !el || el->kind != TURDOM_ELEMENT) return;
    turdom_attr *a = (turdom_attr *)arena_alloc(d, sizeof(turdom_attr));
    if (!a) return;
    a->name = arena_strdup_opt(d, qname ? qname : "");
    a->ns_uri = (ns_uri && ns_uri[0]) ? arena_strdup_opt(d, ns_uri) : NULL;
    a->local = local ? arena_strdup_opt(d, local) : a->name;
    a->value = arena_strndup(d, value ? value : "", value ? vlen : 0);
    a->vlen = value ? vlen : 0;
    a->next = NULL;
    if (!a->name || !a->local || !a->value) return;
    if (el->attrs_tail) el->attrs_tail->next = a;
    else el->attrs = a;
    el->attrs_tail = a;
    el->nattrs++;
}

turdom_node *turdom_append_chars(turdom_doc *d, turdom_node *parent, int kind,
                                 const char *s, size_t n,
                                 int64_t line, int64_t col) {
    if (!parent) return NULL;
    /* Adjacent text merges into one node, as in the DOM. */
    if (kind == TURDOM_TEXT && parent->last &&
        parent->last->kind == TURDOM_TEXT) {
        turdom_node *t = parent->last;
        char *joined = (char *)arena_alloc(d, t->value_len + n + 1);
        if (!joined) return NULL;
        memcpy(joined, t->value, t->value_len);
        if (n) memcpy(joined + t->value_len, s, n);
        joined[t->value_len + n] = '\0';
        t->value = joined;
        t->value_len += n;
        return t;
    }
    turdom_node *t = new_node(d, parent, kind, line, col);
    if (!t) return NULL;
    t->value = arena_strndup(d, s ? s : "", s ? n : 0);
    t->value_len = s ? n : 0;
    if (!t->value) return NULL;
    return t;
}

turdom_node *turdom_append_pi(turdom_doc *d, turdom_node *parent,
                              const char *target, const char *data,
                              int64_t line, int64_t col) {
    turdom_node *n = new_node(d, parent, TURDOM_PI, line, col);
    if (!n) return NULL;
    n->name = arena_strdup_opt(d, target ? target : "");
    n->local = n->name;
    const char *dd = data ? data : "";
    n->value_len = strlen(dd);
    n->value = arena_strndup(d, dd, n->value_len);
    if (!n->name || !n->value) return NULL;
    return n;
}

turdom_node *turdom_append_doctype(turdom_doc *d, turdom_node *parent,
                                   const char *name, const char *public_id,
                                   const char *system_id,
                                   int64_t line, int64_t col) {
    turdom_node *n = new_node(d, parent, TURDOM_DOCTYPE, line, col);
    if (!n) return NULL;
    n->name = arena_strdup_opt(d, name ? name : "");
    n->local = n->name;
    n->public_id = arena_strdup_opt(d, public_id);
    n->system_id = arena_strdup_opt(d, system_id);
    if (!n->name) return NULL;
    return n;
}

/* ---- reading ----------------------------------------------------------- */

int turdom_kind(const turdom_node *n) { return n ? n->kind : -1; }
const char *turdom_name(const turdom_node *n) { return n ? n->name : NULL; }
const char *turdom_local_name(const turdom_node *n) { return n ? n->local : NULL; }
const char *turdom_ns_uri(const turdom_node *n) { return n ? n->ns_uri : NULL; }
const char *turdom_value(const turdom_node *n) {
    if (!n) return NULL;
    switch (n->kind) {
    case TURDOM_TEXT: case TURDOM_COMMENT: case TURDOM_CDATA: case TURDOM_PI:
        return n->value;
    default:
        return NULL;
    }
}
int64_t turdom_value_len(const turdom_node *n) {
    return turdom_value(n) ? (int64_t)n->value_len : 0;
}
const char *turdom_public_id(const turdom_node *n) { return n ? n->public_id : NULL; }
const char *turdom_system_id(const turdom_node *n) { return n ? n->system_id : NULL; }
int64_t turdom_line(const turdom_node *n) { return n ? n->line : 0; }
int64_t turdom_col(const turdom_node *n) { return n ? n->col : 0; }

turdom_node *turdom_parent(const turdom_node *n) { return n ? n->parent : NULL; }
turdom_node *turdom_first_child(const turdom_node *n) { return n ? n->first : NULL; }
turdom_node *turdom_last_child(const turdom_node *n) { return n ? n->last : NULL; }
turdom_node *turdom_next_sibling(const turdom_node *n) { return n ? n->next : NULL; }
turdom_node *turdom_prev_sibling(const turdom_node *n) { return n ? n->prev : NULL; }
int64_t turdom_child_count(const turdom_node *n) { return n ? n->nchildren : 0; }

turdom_node *turdom_root_element(turdom_doc *d) {
    if (!d) return NULL;
    for (turdom_node *c = d->docnode->first; c; c = c->next)
        if (c->kind == TURDOM_ELEMENT) return c;
    return NULL;
}

int64_t turdom_depth(const turdom_node *n) {
    int64_t k = 0;
    while (n && n->parent) { k++; n = n->parent; }
    return k;
}

static const turdom_attr *attr_at(const turdom_node *n, int64_t i) {
    if (!n || n->kind != TURDOM_ELEMENT || i < 0 || i >= n->nattrs) return NULL;
    const turdom_attr *a = n->attrs;
    while (i-- > 0 && a) a = a->next;
    return a;
}

int64_t turdom_attr_count(const turdom_node *n) {
    return (n && n->kind == TURDOM_ELEMENT) ? n->nattrs : 0;
}
const char *turdom_attr_name_at(const turdom_node *n, int64_t i) {
    const turdom_attr *a = attr_at(n, i); return a ? a->name : NULL;
}
const char *turdom_attr_value_at(const turdom_node *n, int64_t i) {
    const turdom_attr *a = attr_at(n, i); return a ? a->value : NULL;
}
const char *turdom_attr_ns_uri_at(const turdom_node *n, int64_t i) {
    const turdom_attr *a = attr_at(n, i); return a ? a->ns_uri : NULL;
}
const char *turdom_attr_local_at(const turdom_node *n, int64_t i) {
    const turdom_attr *a = attr_at(n, i); return a ? a->local : NULL;
}

static int ascii_lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int name_eq(const char *a, const char *b, size_t blen, int fold) {
    if (!a) return 0;
    size_t i = 0;
    for (; i < blen; i++) {
        if (!a[i]) return 0;
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (fold) { x = ascii_lower(x); y = ascii_lower(y); }
        if (x != y) return 0;
    }
    return a[i] == '\0';
}

/* Does (qname, ns_uri, local) answer to `want`?  Accepts the qualified name
 * as written and the Clark form `{uri}local`. */
static int qname_matches(const char *qname, const char *ns_uri,
                         const char *local, const char *want, int fold) {
    if (!want) return 0;
    if (name_eq(qname, want, strlen(want), fold)) return 1;
    if (want[0] == '{' && ns_uri) {
        const char *close = strchr(want, '}');
        if (!close) return 0;
        size_t ulen = (size_t)(close - want - 1);
        if (strlen(ns_uri) != ulen || memcmp(ns_uri, want + 1, ulen) != 0)
            return 0;
        return name_eq(local, close + 1, strlen(close + 1), fold);
    }
    return 0;
}

int turdom_element_matches(const turdom_node *n, const char *name) {
    if (!n || n->kind != TURDOM_ELEMENT || !name) return 0;
    if (name[0] == '*' && name[1] == '\0') return 1;
    int fold = n->doc && n->doc->flavor == TURDOM_FLAVOR_HTML;
    return qname_matches(n->name, n->ns_uri, n->local, name, fold);
}

const char *turdom_attr_get(const turdom_node *n, const char *name) {
    if (!n || n->kind != TURDOM_ELEMENT || !name) return NULL;
    int fold = n->doc && n->doc->flavor == TURDOM_FLAVOR_HTML;
    for (const turdom_attr *a = n->attrs; a; a = a->next)
        if (qname_matches(a->name, a->ns_uri, a->local, name, fold))
            return a->value;
    return NULL;
}

/* ---- walks ------------------------------------------------------------- */

turdom_node *turdom_next_in(const turdom_node *root, const turdom_node *from) {
    if (!root) return NULL;
    const turdom_node *n = from ? from : root;
    if (n->first) return n->first;
    while (n && n != root) {
        if (n->next) return n->next;
        n = n->parent;
    }
    return NULL;
}

static turdom_node *deepest_last(const turdom_node *n) {
    while (n->last) n = n->last;
    return (turdom_node *)n;
}

turdom_node *turdom_prev_in(const turdom_node *root, const turdom_node *from) {
    if (!root) return NULL;
    if (!from) return root->last ? deepest_last(root) : NULL;
    if (from == root) return NULL;
    if (from->prev) return deepest_last(from->prev);
    if (!from->parent || from->parent == root) return NULL;
    return from->parent;
}

turdom_node *turdom_find_next(const turdom_node *root, const turdom_node *from,
                              const char *name) {
    turdom_node *n = turdom_next_in(root, from);
    while (n && !turdom_element_matches(n, name)) n = turdom_next_in(root, n);
    return n;
}

turdom_node *turdom_find_prev(const turdom_node *root, const turdom_node *from,
                              const char *name) {
    turdom_node *n = turdom_prev_in(root, from);
    while (n && !turdom_element_matches(n, name)) n = turdom_prev_in(root, n);
    return n;
}

/* ---- text -------------------------------------------------------------- */

char *turdom_text_content(const turdom_node *n, size_t *len) {
    obuf b = {0};
    if (n) {
        if (n->kind == TURDOM_TEXT || n->kind == TURDOM_CDATA ||
            n->kind == TURDOM_COMMENT || n->kind == TURDOM_PI) {
            ob_put(&b, n->value, n->value_len);
        } else {
            for (const turdom_node *c = turdom_next_in(n, NULL); c;
                 c = turdom_next_in(n, c))
                if (c->kind == TURDOM_TEXT || c->kind == TURDOM_CDATA)
                    ob_put(&b, c->value, c->value_len);
        }
    }
    return ob_finish(&b, len);
}

/* ---- serialization ------------------------------------------------------ */

static int is_html_ns(const turdom_node *n) {
    return n->ns_uri == NULL ||
           strcmp(n->ns_uri, "http://www.w3.org/1999/xhtml") == 0;
}

static int in_set(const char *name, const char *const *set) {
    for (; *set; set++)
        if (strcmp(name, *set) == 0) return 1;
    return 0;
}

static const char *const html_void[] = {
    "area", "base", "basefont", "bgsound", "br", "col", "embed", "frame",
    "hr", "img", "input", "keygen", "link", "meta", "param", "source",
    "track", "wbr", NULL
};

static const char *const html_rawtext[] = {
    "style", "script", "xmp", "iframe", "noembed", "noframes", "plaintext",
    NULL
};

static int html_is_void(const turdom_node *n) {
    return n->kind == TURDOM_ELEMENT && n->doc->flavor == TURDOM_FLAVOR_HTML &&
           is_html_ns(n) && in_set(n->local, html_void);
}

/* XML: & < > always; in attributes also " and the whitespace that attribute
 * normalization would otherwise fold (\t \n \r).  HTML: the fragment
 * serialization algorithm -- & and U+00A0 everywhere, < > in text, and " in
 * attributes (plus < > there too, as current browsers do). */
static void escape_into(obuf *b, const char *s, size_t n, int attr, int html) {
    size_t run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *rep = NULL;
        size_t skip = 0;
        switch (c) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': if (attr) rep = "&quot;"; break;
        case '\t': if (attr && !html) rep = "&#9;"; break;
        case '\n': if (attr && !html) rep = "&#10;"; break;
        case '\r': if (!html) rep = "&#13;"; break;
        case 0xC2:
            if (html && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) {
                rep = "&nbsp;";
                skip = 1;
            }
            break;
        default: break;
        }
        if (rep) {
            ob_put(b, s + run, i - run);
            ob_puts(b, rep);
            i += skip;
            run = i + 1;
        }
    }
    ob_put(b, s + run, n - run);
}

static void put_cdata(obuf *b, const char *s, size_t n) {
    ob_puts(b, "<![CDATA[");
    size_t run = 0;
    for (size_t i = 0; i + 2 < n; i++) {
        if (s[i] == ']' && s[i + 1] == ']' && s[i + 2] == '>') {
            ob_put(b, s + run, i + 2 - run);
            ob_puts(b, "]]><![CDATA[");
            run = i + 2;
        }
    }
    ob_put(b, s + run, n - run);
    ob_puts(b, "]]>");
}

static void put_quoted_id(obuf *b, const char *s) {
    char q = strchr(s, '"') ? '\'' : '"';
    ob_put(b, &q, 1);
    ob_puts(b, s);
    ob_put(b, &q, 1);
}

static void open_node(obuf *b, const turdom_node *n, int has_kids) {
    int html = n->doc->flavor == TURDOM_FLAVOR_HTML;
    switch (n->kind) {
    case TURDOM_DOCUMENT:
        return;
    case TURDOM_ELEMENT: {
        ob_puts(b, "<");
        ob_puts(b, n->name);
        for (const turdom_attr *a = n->attrs; a; a = a->next) {
            ob_puts(b, " ");
            ob_puts(b, a->name);
            ob_puts(b, "=\"");
            escape_into(b, a->value, a->vlen, 1, html);
            ob_puts(b, "\"");
        }
        if (!html && !has_kids) ob_puts(b, "/>");
        else ob_puts(b, ">");
        return;
    }
    case TURDOM_TEXT: {
        const turdom_node *p = n->parent;
        if (html && p && p->kind == TURDOM_ELEMENT && is_html_ns(p) &&
            in_set(p->local, html_rawtext))
            ob_put(b, n->value, n->value_len);
        else
            escape_into(b, n->value, n->value_len, 0, html);
        return;
    }
    case TURDOM_COMMENT:
        ob_puts(b, "<!--");
        ob_put(b, n->value, n->value_len);
        ob_puts(b, "-->");
        return;
    case TURDOM_CDATA:
        put_cdata(b, n->value, n->value_len);
        return;
    case TURDOM_PI:
        ob_puts(b, "<?");
        ob_puts(b, n->name);
        if (n->value_len) {
            ob_puts(b, " ");
            ob_put(b, n->value, n->value_len);
        }
        ob_puts(b, html ? ">" : "?>");
        return;
    case TURDOM_DOCTYPE:
        ob_puts(b, "<!DOCTYPE ");
        ob_puts(b, n->name);
        if (!html) {
            if (n->public_id) {
                ob_puts(b, " PUBLIC ");
                put_quoted_id(b, n->public_id);
                ob_puts(b, " ");
                put_quoted_id(b, n->system_id ? n->system_id : "");
            } else if (n->system_id) {
                ob_puts(b, " SYSTEM ");
                put_quoted_id(b, n->system_id);
            }
        }
        ob_puts(b, ">");
        return;
    default:
        return;
    }
}

static void close_node(obuf *b, const turdom_node *n, int had_kids) {
    if (n->kind != TURDOM_ELEMENT) return;
    int html = n->doc->flavor == TURDOM_FLAVOR_HTML;
    if (html) {
        if (html_is_void(n)) return;
    } else if (!had_kids) {
        return;                 /* already closed as <x/> */
    }
    ob_puts(b, "</");
    ob_puts(b, n->name);
    ob_puts(b, ">");
}

char *turdom_serialize(const turdom_node *top, size_t *len) {
    obuf b = {0};
    if (!top) return ob_finish(&b, len);
    const turdom_node *n = top;
    for (;;) {
        /* An HTML void element's children (only reachable by building the
         * tree by hand) are not serialized, matching the spec. */
        int descend = n->first != NULL && !html_is_void(n);
        open_node(&b, n, descend);
        if (descend) { n = n->first; continue; }
        close_node(&b, n, 0);
        for (;;) {
            if (n == top) return ob_finish(&b, len);
            if (n->next) { n = n->next; break; }
            n = n->parent;
            close_node(&b, n, 1);
        }
    }
}

/* ---- equality ----------------------------------------------------------- */

static int str_eq_opt(const char *a, const char *b) {
    if (!a || !b) return a == b;
    return strcmp(a, b) == 0;
}

static int shallow_eq(const turdom_node *x, const turdom_node *y) {
    if (x->kind != y->kind) return 0;
    if (x->nchildren != y->nchildren) return 0;
    switch (x->kind) {
    case TURDOM_DOCUMENT:
        return 1;
    case TURDOM_ELEMENT: {
        if (!str_eq_opt(x->name, y->name) || !str_eq_opt(x->ns_uri, y->ns_uri))
            return 0;
        if (x->nattrs != y->nattrs) return 0;
        const turdom_attr *a = x->attrs, *c = y->attrs;
        for (; a && c; a = a->next, c = c->next) {
            if (!str_eq_opt(a->name, c->name) ||
                !str_eq_opt(a->ns_uri, c->ns_uri) || a->vlen != c->vlen ||
                memcmp(a->value, c->value, a->vlen) != 0)
                return 0;
        }
        return 1;
    }
    case TURDOM_DOCTYPE:
        return str_eq_opt(x->name, y->name) &&
               str_eq_opt(x->public_id, y->public_id) &&
               str_eq_opt(x->system_id, y->system_id);
    case TURDOM_PI:
        if (!str_eq_opt(x->name, y->name)) return 0;
        /* fallthrough */
    default:
        return x->value_len == y->value_len &&
               memcmp(x->value, y->value, x->value_len) == 0;
    }
}

int turdom_equal(const turdom_node *a, const turdom_node *b) {
    if (!a || !b) return a == b;
    const turdom_node *x = a, *y = b;
    while (x && y) {
        if (!shallow_eq(x, y)) return 0;
        x = turdom_next_in(a, x);
        y = turdom_next_in(b, y);
    }
    return x == NULL && y == NULL;
}
