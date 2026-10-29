/* c/dom/turdom_select.c -- CSS selectors over the turdom tree.  See turdom.h
 * ("selectors") and turmeric docs/upcoming/spices/xml-html-followups-plan.md,
 * follow-up C.
 *
 * Three parts: a parser that reads a selector list straight into a compiled
 * form (CSS Syntax's tokenizer rules for identifiers, escapes and strings,
 * applied as the grammar needs them), a right-to-left matcher, and
 * node-css-path.  Everything a compiled selector holds lives in one small
 * arena, freed with it.
 *
 * Recursion is bounded by the selector, never by the document: the parser and
 * the matcher recurse once per nested :not()/:is()/:where()/:nth-*(of S)/
 * :has() (at most SEL_MAX_DEPTH) and the matcher once per compound of a
 * complex selector (at most SEL_MAX_COMPOUNDS).  Every walk over the tree is a
 * loop.
 */

#include "turdom.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEL_MAX_DEPTH     32
#define SEL_MAX_COMPOUNDS 64
/* :has() gives up after this many candidate tests between two
 * turdom_selector_reset calls; see turdom_selector_budget_hit. */
#define SEL_HAS_BUDGET    ((int64_t)1000000)

/* ---- compiled form -------------------------------------------------------- */

enum {
    S_TYPE,        /* name (local, or qualified when it holds a ':') + ns */
    S_UNIVERSAL,   /* only the ns constraint: `ns|*`, `|*` */
    S_ID,
    S_CLASS,
    S_ATTR,
    S_ROOT,
    S_SCOPE,
    S_EMPTY,
    S_NTH,         /* nth-child and friends; see the nth_* fields */
    S_NOT,
    S_IS,          /* :is() and :where() -- specificity is not computed */
    S_HAS,
    S_ANCHOR       /* internal: the :has() subject (leftmost of a relative selector) */
};

enum { NS_ANY, NS_NONE, NS_URI };

enum {
    A_EXISTS, A_EQ, A_INCLUDES, A_DASH, A_PREFIX, A_SUFFIX, A_SUBSTR
};

enum { C_NONE = 0, C_DESC = ' ', C_CHILD = '>', C_ADJ = '+', C_SIB = '~' };

typedef struct sel_list sel_list;

typedef struct {
    int kind;
    int ns_mode;
    const char *ns_uri;
    const char *name;    /* decoded */
    int qualified;       /* name holds ':' -> compared with the qualified name */
    int op;
    const char *value;
    int flag;            /* 0, 'i' or 's' */
    int64_t a, b;        /* S_NTH: An+B */
    int nth_last;        /* count from the end */
    int nth_type;        /* count only siblings of the same type */
    sel_list *sub;       /* :not/:is/:where/:has, and nth-child(.. of S) */
} sel_simple;

typedef struct {
    sel_simple *s;
    int n, cap;
    int comb;            /* combinator between the previous compound and this */
} sel_compound;

typedef struct {
    sel_compound *c;
    int n, cap;
} sel_complex;

struct sel_list {
    sel_complex *x;
    int n, cap;
};

typedef struct sel_chunk {
    struct sel_chunk *next;
    size_t used, cap;
    unsigned char data[];
} sel_chunk;

struct turdom_selector {
    sel_chunk *chunks;
    sel_list *list;
    char *err;             /* malloc'd; NULL when the selector compiled */
    int64_t err_off;
    int64_t budget;        /* :has() steps left in the current match */
    int budget_hit;        /* a :has() gave up since the last reset */
    int has_has;           /* the selector uses :has() */
};

struct turdom_nsmap {
    char **prefix;
    char **uri;
    size_t n, cap;
};

static void *sel_alloc(turdom_selector *S, size_t n) {
    n = (n + 7u) & ~(size_t)7u;
    if (!n) n = 8;
    sel_chunk *c = S->chunks;
    if (c && c->used + n <= c->cap) {
        void *p = c->data + c->used;
        c->used += n;
        return p;
    }
    size_t cap = n > 4096 ? n : 4096;
    sel_chunk *nc = (sel_chunk *)malloc(sizeof(sel_chunk) + cap);
    if (!nc) return NULL;
    nc->cap = cap;
    nc->used = n;
    nc->next = c;
    S->chunks = nc;
    memset(nc->data, 0, n);
    return nc->data;
}

static void *sel_zalloc(turdom_selector *S, size_t n) {
    void *p = sel_alloc(S, n);
    if (p) memset(p, 0, n);
    return p;
}

/* Grow a dynamic array held in the arena (the old block is abandoned). */
static int sel_grow(turdom_selector *S, void **arr, int *cap, int n, size_t elt) {
    if (n < *cap) return 1;
    int ncap = *cap ? *cap * 2 : 4;
    void *p = sel_zalloc(S, (size_t)ncap * elt);
    if (!p) return 0;
    if (*arr && n) memcpy(p, *arr, (size_t)n * elt);
    *arr = p;
    *cap = ncap;
    return 1;
}

/* ---- the parser ----------------------------------------------------------- */

typedef struct {
    turdom_selector *S;
    const char *src;
    size_t len, pos;
    const turdom_nsmap *ns;
    const char *default_ns;  /* the "" prefix's URI, or NULL */
    int depth;
    int in_has;
    int failed;
} P;

static int fail_at(P *p, size_t at, const char *fmt, ...) {
    if (p->failed) return 0;
    p->failed = 1;
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    p->S->err = (char *)malloc(strlen(buf) + 1);
    if (p->S->err) strcpy(p->S->err, buf);
    p->S->err_off = (int64_t)at;
    return 0;
}

static int oom(P *p) { return fail_at(p, p->pos, "out of memory"); }

static int peek(const P *p, size_t k) {
    return p->pos + k < p->len ? (unsigned char)p->src[p->pos + k] : -1;
}

static int is_ws(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int is_hex(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
}

static int name_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c >= 0x80;
}

static int name_char(int c) {
    return name_start(c) || (c >= '0' && c <= '9') || c == '-';
}

/* A valid escape at p->pos + k: a backslash not followed by a newline. */
static int escape_at(const P *p, size_t k) {
    return peek(p, k) == '\\' && peek(p, k + 1) != '\n' &&
           peek(p, k + 1) != '\r' && peek(p, k + 1) != '\f';
}

/* Would an identifier start at p->pos + k? */
static int ident_start_at(const P *p, size_t k) {
    int c = peek(p, k);
    if (c == '-') {
        int d = peek(p, k + 1);
        return name_start(d) || d == '-' || escape_at(p, k + 1);
    }
    return name_start(c) || escape_at(p, k);
}

/* Whitespace and comments.  Returns whether any was skipped. */
static int skip_ws(P *p) {
    size_t start = p->pos;
    for (;;) {
        if (is_ws(peek(p, 0))) {
            p->pos++;
        } else if (peek(p, 0) == '/' && peek(p, 1) == '*') {
            const char *end = NULL;
            for (size_t i = p->pos + 2; i + 1 < p->len; i++)
                if (p->src[i] == '*' && p->src[i + 1] == '/') { end = p->src + i; break; }
            if (!end) return fail_at(p, p->pos, "unterminated comment"), 1;
            p->pos = (size_t)(end - p->src) + 2;
        } else {
            break;
        }
    }
    return p->pos != start;
}

/* A growable string in the arena. */
typedef struct { char *s; int n, cap; } sbuf;

static int sb_put(P *p, sbuf *b, const char *s, size_t n) {
    while (b->n + (int)n + 1 > b->cap) {
        int ncap = b->cap ? b->cap * 2 : 32;
        char *ns = (char *)sel_alloc(p->S, (size_t)ncap);
        if (!ns) return oom(p);
        if (b->n) memcpy(ns, b->s, (size_t)b->n);
        b->s = ns;
        b->cap = ncap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += (int)n;
    b->s[b->n] = '\0';
    return 1;
}

static int sb_cp(P *p, sbuf *b, uint32_t cp) {
    char u[4];
    size_t n;
    if (cp == 0 || (cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = 0xFFFD;
    if (cp < 0x80) { u[0] = (char)cp; n = 1; }
    else if (cp < 0x800) { u[0] = (char)(0xC0 | (cp >> 6)); u[1] = (char)(0x80 | (cp & 0x3F)); n = 2; }
    else if (cp < 0x10000) {
        u[0] = (char)(0xE0 | (cp >> 12)); u[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        u[2] = (char)(0x80 | (cp & 0x3F)); n = 3;
    } else {
        u[0] = (char)(0xF0 | (cp >> 18)); u[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        u[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); u[3] = (char)(0x80 | (cp & 0x3F)); n = 4;
    }
    return sb_put(p, b, u, n);
}

/* Consume an escape (p->pos at the backslash) into b. */
static int read_escape(P *p, sbuf *b) {
    p->pos++;
    int c = peek(p, 0);
    if (c < 0) return sb_cp(p, b, 0xFFFD);
    if (is_hex(c)) {
        uint32_t v = 0;
        int k = 0;
        while (k < 6 && is_hex(peek(p, 0))) { v = v * 16 + (uint32_t)hex_val(peek(p, 0)); p->pos++; k++; }
        if (peek(p, 0) == '\r' && peek(p, 1) == '\n') p->pos += 2;
        else if (is_ws(peek(p, 0))) p->pos++;
        return sb_cp(p, b, v);
    }
    /* Copy the escaped character, all of its UTF-8 bytes. */
    size_t n = 1;
    if (c >= 0xC0) {
        n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
        if (p->pos + n > p->len) n = p->len - p->pos;
    }
    int ok = sb_put(p, b, p->src + p->pos, n);
    p->pos += n;
    return ok;
}

/* Name code points and escapes (the caller checked the start).  Returns the
 * decoded string, never empty, or NULL after an error. */
static const char *read_name(P *p) {
    sbuf b = {0};
    for (;;) {
        int c = peek(p, 0);
        if (name_char(c)) {
            size_t s = p->pos;
            while (name_char(peek(p, 0))) p->pos++;
            if (!sb_put(p, &b, p->src + s, p->pos - s)) return NULL;
        } else if (escape_at(p, 0)) {
            if (!read_escape(p, &b)) return NULL;
        } else {
            break;
        }
    }
    if (!b.s) { fail_at(p, p->pos, "expected a name"); return NULL; }
    return b.s;
}

static const char *read_ident(P *p) {
    if (!ident_start_at(p, 0)) {
        fail_at(p, p->pos, "expected an identifier");
        return NULL;
    }
    return read_name(p);
}

/* A quoted string (p->pos at the quote). */
static const char *read_string(P *p) {
    int q = peek(p, 0);
    p->pos++;
    sbuf b = {0};
    if (!sb_put(p, &b, "", 0)) return NULL;
    for (;;) {
        int c = peek(p, 0);
        if (c < 0) return b.s;  /* EOF ends a string (CSS Syntax 4.3.5) */
        if (c == q) { p->pos++; return b.s; }
        if (c == '\n' || c == '\r' || c == '\f') {
            fail_at(p, p->pos, "newline in a string");
            return NULL;
        }
        if (c == '\\') {
            int d = peek(p, 1);
            if (d < 0) { p->pos++; continue; }
            if (d == '\n' || d == '\f') { p->pos += 2; continue; }
            if (d == '\r') { p->pos += peek(p, 2) == '\n' ? 3 : 2; continue; }
            if (!read_escape(p, &b)) return NULL;
            continue;
        }
        size_t s = p->pos;
        while (peek(p, 0) >= 0 && peek(p, 0) != q && peek(p, 0) != '\\' &&
               peek(p, 0) != '\n' && peek(p, 0) != '\r' && peek(p, 0) != '\f')
            p->pos++;
        if (!sb_put(p, &b, p->src + s, p->pos - s)) return NULL;
    }
}

static int ascii_lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int ieq(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (ascii_lower_c((unsigned char)*a) != ascii_lower_c((unsigned char)*b))
            return 0;
    return *a == *b;
}

static const char *ns_lookup_prefix(P *p, const char *prefix, size_t at) {
    if (p->ns)
        for (size_t i = 0; i < p->ns->n; i++)
            if (strcmp(p->ns->prefix[i], prefix) == 0) return p->ns->uri[i];
    fail_at(p, at, "unknown namespace prefix '%s' (selector-compile-ns declares prefixes)", prefix);
    return NULL;
}

static sel_list *parse_list(P *p, int relative);

static sel_simple *add_simple(P *p, sel_compound *c, int kind) {
    if (!sel_grow(p->S, (void **)&c->s, &c->cap, c->n, sizeof(sel_simple))) {
        oom(p);
        return NULL;
    }
    sel_simple *s = &c->s[c->n++];
    memset(s, 0, sizeof *s);
    s->kind = kind;
    return s;
}

/* An optional namespace prefix before a type or attribute name: `p|`, `*|`
 * or `|` (not `|=`, nor `||`).  Sets the mode and URI and returns 1 when one was
 * read; 0 with nothing consumed when there is none. */
static int read_ns_prefix(P *p, int *mode, const char **uri, int for_attr) {
    size_t save = p->pos;
    if (peek(p, 0) == '|' && peek(p, 1) != '=' && peek(p, 1) != '|') {
        p->pos++;
        *mode = NS_NONE;
        *uri = NULL;
        return 1;
    }
    if (peek(p, 0) == '*' && peek(p, 1) == '|' && peek(p, 2) != '=' && peek(p, 2) != '|') {
        p->pos += 2;
        *mode = NS_ANY;
        *uri = NULL;
        return 1;
    }
    if (ident_start_at(p, 0)) {
        const char *name = read_name(p);
        if (!name) return -1;
        if (peek(p, 0) == '|' && peek(p, 1) != '=' && peek(p, 1) != '|' &&
            (ident_start_at(p, 1) || (!for_attr && peek(p, 1) == '*'))) {
            const char *u = ns_lookup_prefix(p, name, save);
            if (!u) return -1;
            p->pos++;
            *mode = NS_URI;
            *uri = u;
            return 1;
        }
    }
    p->pos = save;
    return 0;
}

/* Type selector or universal, with an optional namespace prefix. */
static int parse_type(P *p, sel_compound *c, int *had) {
    int mode = NS_ANY;
    const char *uri = NULL;
    int r = read_ns_prefix(p, &mode, &uri, 0);
    if (r < 0) return 0;
    int explicit_ns = r == 1;
    if (!explicit_ns && p->default_ns) { mode = NS_URI; uri = p->default_ns; }
    if (peek(p, 0) == '*') {
        p->pos++;
        *had = 1;
        if (mode == NS_ANY) return 1;
        sel_simple *s = add_simple(p, c, S_UNIVERSAL);
        if (!s) return 0;
        s->ns_mode = mode;
        s->ns_uri = uri;
        return 1;
    }
    if (ident_start_at(p, 0)) {
        const char *name = read_name(p);
        if (!name) return 0;
        sel_simple *s = add_simple(p, c, S_TYPE);
        if (!s) return 0;
        s->name = name;
        s->qualified = strchr(name, ':') != NULL;
        /* A name written with an escaped colon is the qualified name as
         * written: no namespace constraint unless one was asked for. */
        s->ns_mode = (s->qualified && !explicit_ns) ? NS_ANY : mode;
        s->ns_uri = s->ns_mode == NS_URI ? uri : NULL;
        *had = 1;
        return 1;
    }
    if (explicit_ns) return fail_at(p, p->pos, "expected a name or '*' after the namespace prefix");
    /* No type selector: the implied `*` still means the default namespace. */
    if (p->default_ns) {
        sel_simple *s = add_simple(p, c, S_UNIVERSAL);
        if (!s) return 0;
        s->ns_mode = NS_URI;
        s->ns_uri = p->default_ns;
    }
    return 1;
}

static int parse_attr(P *p, sel_compound *c) {
    p->pos++;
    skip_ws(p);
    if (p->failed) return 0;
    int mode = NS_NONE;
    const char *uri = NULL;
    int r = read_ns_prefix(p, &mode, &uri, 1);
    if (r < 0) return 0;
    if (r == 0) mode = NS_NONE;
    if (!ident_start_at(p, 0)) return fail_at(p, p->pos, "expected an attribute name");
    const char *name = read_name(p);
    if (!name) return 0;
    sel_simple *s = add_simple(p, c, S_ATTR);
    if (!s) return 0;
    s->name = name;
    s->qualified = strchr(name, ':') != NULL;
    s->ns_mode = (s->qualified && r == 0) ? NS_ANY : mode;
    s->ns_uri = uri;
    s->op = A_EXISTS;
    skip_ws(p);
    if (p->failed) return 0;
    int ch = peek(p, 0);
    if (ch == ']') { p->pos++; return 1; }
    if (ch < 0) return 1;  /* EOF closes the block */
    if (ch == '=') { s->op = A_EQ; p->pos++; }
    else if (peek(p, 1) == '=' && (ch == '~' || ch == '|' || ch == '^' || ch == '$' || ch == '*')) {
        s->op = ch == '~' ? A_INCLUDES : ch == '|' ? A_DASH : ch == '^' ? A_PREFIX
              : ch == '$' ? A_SUFFIX : A_SUBSTR;
        p->pos += 2;
    } else {
        return fail_at(p, p->pos, "expected an attribute operator or ']'");
    }
    skip_ws(p);
    if (p->failed) return 0;
    ch = peek(p, 0);
    if (ch == '"' || ch == '\'') s->value = read_string(p);
    else if (ident_start_at(p, 0)) s->value = read_name(p);
    else return fail_at(p, p->pos, "expected an attribute value (an identifier or a quoted string)");
    if (!s->value) return 0;
    skip_ws(p);
    if (p->failed) return 0;
    if (ident_start_at(p, 0)) {
        size_t at = p->pos;
        const char *f = read_name(p);
        if (!f) return 0;
        if (ieq(f, "i")) s->flag = 'i';
        else if (ieq(f, "s")) s->flag = 's';
        else return fail_at(p, at, "unknown attribute selector flag '%s' (use i or s)", f);
        skip_ws(p);
        if (p->failed) return 0;
    }
    if (peek(p, 0) < 0) return 1;  /* EOF closes the block */
    if (peek(p, 0) != ']') return fail_at(p, p->pos, "expected ']'");
    p->pos++;
    return 1;
}

static int read_int(P *p, int64_t *out) {
    if (!(peek(p, 0) >= '0' && peek(p, 0) <= '9')) return 0;
    int64_t v = 0;
    while (peek(p, 0) >= '0' && peek(p, 0) <= '9') {
        if (v < (int64_t)1 << 40) v = v * 10 + (peek(p, 0) - '0');
        p->pos++;
    }
    *out = v;
    return 1;
}

/* An+B (CSS Syntax 3, section 6), stopping before `of` or `)`. */
static int parse_anb(P *p, sel_simple *s) {
    size_t start = p->pos;
    skip_ws(p);
    if (p->failed) return 0;
    if (ident_start_at(p, 0)) {
        size_t save = p->pos;
        const char *w = read_name(p);
        if (!w) return 0;
        if (ieq(w, "odd")) { s->a = 2; s->b = 1; return 1; }
        if (ieq(w, "even")) { s->a = 2; s->b = 0; return 1; }
        p->pos = save;
    }
    int64_t sign = 1;
    if (peek(p, 0) == '+' || peek(p, 0) == '-') {
        sign = peek(p, 0) == '-' ? -1 : 1;
        p->pos++;
    }
    int64_t a = 0;
    int have_digits = read_int(p, &a);
    if (peek(p, 0) == 'n' || peek(p, 0) == 'N') {
        p->pos++;
        s->a = sign * (have_digits ? a : 1);
        size_t save = p->pos;
        int ws = skip_ws(p);
        if (p->failed) return 0;
        int c = peek(p, 0);
        if (c == '+' || c == '-') {
            p->pos++;
            skip_ws(p);
            if (p->failed) return 0;
            int64_t b = 0;
            if (!read_int(p, &b)) return fail_at(p, p->pos, "expected a number after the sign in An+B");
            s->b = c == '-' ? -b : b;
        } else {
            (void)ws;
            p->pos = save;
            s->b = 0;
        }
        return 1;
    }
    if (!have_digits) return fail_at(p, start, "expected An+B, odd or even");
    s->a = 0;
    s->b = sign * a;
    return 1;
}

/* Arguments of a functional pseudo-class (p->pos just after the `(`). */
static int parse_pseudo_fn(P *p, sel_compound *c, const char *name, size_t at) {
    if (++p->depth > SEL_MAX_DEPTH)
        return fail_at(p, at, "selector nested too deeply (more than %d levels)", SEL_MAX_DEPTH);
    int nth_last = 0, nth_type = 0, is_nth = 1;
    if (ieq(name, "nth-child")) {}
    else if (ieq(name, "nth-last-child")) nth_last = 1;
    else if (ieq(name, "nth-of-type")) nth_type = 1;
    else if (ieq(name, "nth-last-of-type")) { nth_last = 1; nth_type = 1; }
    else is_nth = 0;
    if (is_nth) {
        sel_simple *s = add_simple(p, c, S_NTH);
        if (!s) return 0;
        s->nth_last = nth_last;
        s->nth_type = nth_type;
        if (!parse_anb(p, s)) return 0;
        int ws = skip_ws(p);
        if (p->failed) return 0;
        if (!nth_type && ws && ident_start_at(p, 0)) {
            size_t kat = p->pos;
            const char *kw = read_name(p);
            if (!kw) return 0;
            if (!ieq(kw, "of")) return fail_at(p, kat, "expected 'of' or ')' after An+B");
            if (!skip_ws(p)) return fail_at(p, p->pos, "expected a selector list after 'of'");
            if (!(s->sub = parse_list(p, 0))) return 0;
        }
    } else {
        int kind;
        if (ieq(name, "not")) kind = S_NOT;
        else if (ieq(name, "is") || ieq(name, "where") || ieq(name, "matches") ||
                 ieq(name, "-webkit-any")) kind = S_IS;
        else if (ieq(name, "has")) kind = S_HAS;
        else if (ieq(name, "lang") || ieq(name, "dir"))
            return fail_at(p, at, "':%s()' depends on inherited state that is not in the tree; it is not supported", name);
        else if (ieq(name, "host") || ieq(name, "host-context") || ieq(name, "state"))
            return fail_at(p, at, "':%s()' needs a shadow tree or element state; it is not supported", name);
        else return fail_at(p, at, "unknown pseudo-class ':%s()'", name);
        sel_simple *s = add_simple(p, c, kind);
        if (!s) return 0;
        if (kind == S_HAS) {
            /* No :has() inside :has(): the spec forbids it, and it would
             * multiply the budget's reach. */
            if (p->in_has) return fail_at(p, at, "':has()' cannot be nested inside ':has()'");
            p->S->has_has = 1;
            p->in_has++;
            s->sub = parse_list(p, 1);
            p->in_has--;
        } else {
            s->sub = parse_list(p, 0);
        }
        if (!s->sub) return 0;
    }
    skip_ws(p);
    if (p->failed) return 0;
    if (peek(p, 0) >= 0) {  /* EOF closes the function */
        if (peek(p, 0) != ')') return fail_at(p, p->pos, "expected ')'");
        p->pos++;
    }
    p->depth--;
    return 1;
}

static const char *const dynamic_pseudo[] = {
    "hover", "active", "focus", "focus-visible", "focus-within", "visited",
    "link", "any-link", "local-link", "target", "target-within", "checked",
    "default", "indeterminate", "enabled", "disabled", "valid", "invalid",
    "in-range", "out-of-range", "required", "optional", "read-only",
    "read-write", "placeholder-shown", "autofill", "user-invalid",
    "user-valid", "playing", "paused", "fullscreen", "modal",
    "popover-open", "defined", "blank", "current", "past", "future",
    "open", "closed", NULL
};

static const char *const legacy_pseudo_elements[] = {
    "before", "after", "first-line", "first-letter", NULL
};

static int in_list_ci(const char *s, const char *const *set) {
    for (; *set; set++)
        if (ieq(s, *set)) return 1;
    return 0;
}

static int parse_pseudo(P *p, sel_compound *c) {
    size_t at = p->pos;
    p->pos++;
    if (peek(p, 0) == ':') {
        p->pos++;
        const char *pe = ident_start_at(p, 0) ? read_name(p) : NULL;
        return fail_at(p, at, "pseudo-elements are not supported ('::%s'): a selector picks nodes, not parts of them",
                       pe ? pe : "");
    }
    if (!ident_start_at(p, 0)) return fail_at(p, p->pos, "expected a pseudo-class name after ':'");
    const char *name = read_name(p);
    if (!name) return 0;
    if (peek(p, 0) == '(') {
        p->pos++;
        return parse_pseudo_fn(p, c, name, at);
    }
    if (in_list_ci(name, legacy_pseudo_elements))
        return fail_at(p, at, "pseudo-elements are not supported (':%s'): a selector picks nodes, not parts of them", name);
    if (in_list_ci(name, dynamic_pseudo))
        return fail_at(p, at, "':%s' matches dynamic or UI state; a parsed tree has none, so it would never match", name);
    sel_simple *s;
    if (ieq(name, "root")) return add_simple(p, c, S_ROOT) != NULL;
    if (ieq(name, "scope")) return add_simple(p, c, S_SCOPE) != NULL;
    if (ieq(name, "empty")) return add_simple(p, c, S_EMPTY) != NULL;
    int last = 0, type = 0, only = 0;
    if (ieq(name, "first-child")) {}
    else if (ieq(name, "last-child")) last = 1;
    else if (ieq(name, "only-child")) only = 1;
    else if (ieq(name, "first-of-type")) type = 1;
    else if (ieq(name, "last-of-type")) { last = 1; type = 1; }
    else if (ieq(name, "only-of-type")) { only = 1; type = 1; }
    else if (ieq(name, "has") || ieq(name, "not") || ieq(name, "is") ||
             ieq(name, "where") || ieq(name, "nth-child") ||
             ieq(name, "nth-last-child") || ieq(name, "nth-of-type") ||
             ieq(name, "nth-last-of-type"))
        return fail_at(p, at, "':%s' takes an argument: ':%s(...)'", name, name);
    else return fail_at(p, at, "unknown pseudo-class ':%s'", name);
    if (!(s = add_simple(p, c, S_NTH))) return 0;
    s->a = 0; s->b = 1; s->nth_last = last; s->nth_type = type;
    if (only) {
        if (!(s = add_simple(p, c, S_NTH))) return 0;
        s->a = 0; s->b = 1; s->nth_last = 1; s->nth_type = type;
    }
    return 1;
}

static int compound_end(int c) {
    return c < 0 || is_ws(c) || c == ',' || c == ')' || c == '>' || c == '+' ||
           c == '~' || c == '/';
}

static int parse_compound(P *p, sel_compound *c) {
    size_t start = p->pos;
    int had = 0;
    if (!parse_type(p, c, &had)) return 0;
    for (;;) {
        int ch = peek(p, 0);
        if (compound_end(ch)) break;
        if (ch == '#') {
            size_t at = p->pos;
            p->pos++;
            if (!name_char(peek(p, 0)) && !escape_at(p, 0))
                return fail_at(p, at, "expected a name after '#'");
            const char *id = read_name(p);
            if (!id) return 0;
            sel_simple *s = add_simple(p, c, S_ID);
            if (!s) return 0;
            s->name = id;
        } else if (ch == '.') {
            p->pos++;
            const char *cls = read_ident(p);
            if (!cls) return 0;
            sel_simple *s = add_simple(p, c, S_CLASS);
            if (!s) return 0;
            s->name = cls;
        } else if (ch == '[') {
            if (!parse_attr(p, c)) return 0;
        } else if (ch == ':') {
            if (!parse_pseudo(p, c)) return 0;
        } else if (ch == '|' && peek(p, 1) == '|') {
            return fail_at(p, p->pos, "the column combinator '||' is not supported");
        } else if (ch == '*' || ident_start_at(p, 0) || ch == '|') {
            return fail_at(p, p->pos, "a type selector must come first in a compound selector");
        } else {
            return fail_at(p, p->pos, "unexpected '%c'", ch);
        }
        had = 1;
    }
    if (!had) {
        if (peek(p, 0) < 0) return fail_at(p, start, "expected a selector");
        return fail_at(p, p->pos, "expected a selector before '%c'", peek(p, 0));
    }
    return 1;
}

static sel_compound *add_compound(P *p, sel_complex *x) {
    if (x->n >= SEL_MAX_COMPOUNDS) {
        fail_at(p, p->pos, "selector has too many compound parts (more than %d)", SEL_MAX_COMPOUNDS);
        return NULL;
    }
    if (!sel_grow(p->S, (void **)&x->c, &x->cap, x->n, sizeof(sel_compound))) {
        oom(p);
        return NULL;
    }
    sel_compound *c = &x->c[x->n++];
    memset(c, 0, sizeof *c);
    return c;
}

/* One complex selector.  `relative`: it may open with a combinator and is
 * anchored at the :has() subject (an S_ANCHOR compound first). */
static int parse_complex(P *p, sel_complex *x, int relative) {
    int comb = C_NONE;
    if (relative) {
        sel_compound *a = add_compound(p, x);
        if (!a || !add_simple(p, a, S_ANCHOR)) return 0;
        int ch = peek(p, 0);
        comb = C_DESC;
        if (ch == '>' || ch == '+' || ch == '~') {
            comb = ch;
            p->pos++;
            skip_ws(p);
            if (p->failed) return 0;
        }
    }
    for (;;) {
        sel_compound *c = add_compound(p, x);
        if (!c) return 0;
        c->comb = comb;
        if (!parse_compound(p, c)) return 0;
        int ws = skip_ws(p);
        if (p->failed) return 0;
        int ch = peek(p, 0);
        if (ch < 0 || ch == ',' || ch == ')') return 1;
        if (ch == '>' || ch == '+' || ch == '~') {
            comb = ch;
            p->pos++;
            skip_ws(p);
            if (p->failed) return 0;
        } else if (ch == '|' && peek(p, 1) == '|') {
            return fail_at(p, p->pos, "the column combinator '||' is not supported");
        } else if (ws) {
            comb = C_DESC;
        } else {
            return fail_at(p, p->pos, "unexpected '%c'", ch);
        }
    }
}

static sel_list *parse_list(P *p, int relative) {
    sel_list *l = (sel_list *)sel_zalloc(p->S, sizeof(sel_list));
    if (!l) { oom(p); return NULL; }
    skip_ws(p);
    if (p->failed) return NULL;
    for (;;) {
        if (!sel_grow(p->S, (void **)&l->x, &l->cap, l->n, sizeof(sel_complex))) {
            oom(p);
            return NULL;
        }
        sel_complex *x = &l->x[l->n++];
        memset(x, 0, sizeof *x);
        if (peek(p, 0) == ',' || peek(p, 0) == ')' || peek(p, 0) < 0) {
            fail_at(p, p->pos, "expected a selector");
            return NULL;
        }
        if (!parse_complex(p, x, relative)) return NULL;
        if (peek(p, 0) != ',') return l;
        p->pos++;
        skip_ws(p);
        if (p->failed) return NULL;
    }
}

/* ---- namespace maps ------------------------------------------------------- */

turdom_nsmap *turdom_nsmap_new(void) {
    return (turdom_nsmap *)calloc(1, sizeof(turdom_nsmap));
}

static char *dup_str(const char *s) {
    size_t n = strlen(s);
    char *d = (char *)malloc(n + 1);
    if (d) memcpy(d, s, n + 1);
    return d;
}

int turdom_nsmap_add(turdom_nsmap *m, const char *prefix, const char *uri) {
    if (!m || !prefix || !uri) return TURDOM_E_BAD_NAME;
    for (size_t i = 0; i < m->n; i++)
        if (strcmp(m->prefix[i], prefix) == 0) {
            char *u = dup_str(uri);
            if (!u) return TURDOM_E_OOM;
            free(m->uri[i]);
            m->uri[i] = u;
            return TURDOM_OK;
        }
    if (m->n == m->cap) {
        size_t ncap = m->cap ? m->cap * 2 : 4;
        char **np = (char **)realloc(m->prefix, ncap * sizeof(char *));
        if (!np) return TURDOM_E_OOM;
        m->prefix = np;
        char **nu = (char **)realloc(m->uri, ncap * sizeof(char *));
        if (!nu) return TURDOM_E_OOM;
        m->uri = nu;
        m->cap = ncap;
    }
    char *pp = dup_str(prefix), *uu = dup_str(uri);
    if (!pp || !uu) { free(pp); free(uu); return TURDOM_E_OOM; }
    m->prefix[m->n] = pp;
    m->uri[m->n] = uu;
    m->n++;
    return TURDOM_OK;
}

void turdom_nsmap_free(turdom_nsmap *m) {
    if (!m) return;
    for (size_t i = 0; i < m->n; i++) { free(m->prefix[i]); free(m->uri[i]); }
    free(m->prefix);
    free(m->uri);
    free(m);
}

/* ---- compile / free ------------------------------------------------------- */

turdom_selector *turdom_selector_compile(const char *src, const turdom_nsmap *ns) {
    turdom_selector *S = (turdom_selector *)calloc(1, sizeof(turdom_selector));
    if (!S) return NULL;
    S->budget = SEL_HAS_BUDGET;
    P p;
    memset(&p, 0, sizeof p);
    p.S = S;
    p.src = src ? src : "";
    p.len = strlen(p.src);
    p.ns = ns;
    p.default_ns = NULL;
    if (ns)
        for (size_t i = 0; i < ns->n; i++)
            if (ns->prefix[i][0] == '\0') {
                /* The URI is copied into the arena: the map may be freed. */
                sbuf b = {0};
                if (!sb_put(&p, &b, ns->uri[i], strlen(ns->uri[i]))) break;
                p.default_ns = b.s;
            }
    /* Prefix URIs are looked up while parsing; copy them too. */
    turdom_nsmap copy = {0};
    if (ns && ns->n) {
        copy.n = ns->n;
        copy.prefix = (char **)sel_zalloc(S, ns->n * sizeof(char *));
        copy.uri = (char **)sel_zalloc(S, ns->n * sizeof(char *));
        if (!copy.prefix || !copy.uri) oom(&p);
        for (size_t i = 0; i < ns->n && !p.failed; i++) {
            sbuf a = {0}, b = {0};
            sb_put(&p, &a, ns->prefix[i], strlen(ns->prefix[i]));
            sb_put(&p, &b, ns->uri[i], strlen(ns->uri[i]));
            copy.prefix[i] = a.s;
            copy.uri[i] = b.s;
        }
        p.ns = &copy;
    }
    if (!p.failed) {
        skip_ws(&p);
        if (!p.failed && p.pos >= p.len) {
            fail_at(&p, 0, "empty selector");
        } else if (!p.failed) {
            S->list = parse_list(&p, 0);
            if (S->list && p.pos < p.len)
                fail_at(&p, p.pos, peek(&p, 0) == ')' ? "unbalanced ')'" : "unexpected '%c'", peek(&p, 0));
        }
    }
    if (p.failed) S->list = NULL;
    return S;
}

const char *turdom_selector_error(const turdom_selector *s) {
    return s ? s->err : "out of memory";
}

int64_t turdom_selector_error_offset(const turdom_selector *s) {
    return s ? s->err_off : 0;
}

void turdom_selector_free(turdom_selector *s) {
    if (!s) return;
    sel_chunk *c = s->chunks;
    while (c) { sel_chunk *n = c->next; free(c); c = n; }
    free(s->err);
    free(s);
}

int turdom_selector_budget_hit(const turdom_selector *s) {
    return s ? s->budget_hit : 0;
}

/* ---- matching ------------------------------------------------------------- */

#define XHTML_NS "http://www.w3.org/1999/xhtml"

enum { M_MATCH = 0, M_LOCAL, M_ALL_SIBLINGS, M_COMPLETELY };

typedef struct {
    turdom_selector *S;
    const turdom_node *scope;   /* :scope; NULL or the document node -> :root */
    const turdom_node *anchor;  /* the :has() subject */
} M;

static int is_elem(const turdom_node *n) {
    return n && turdom_kind(n) == TURDOM_ELEMENT;
}

static const turdom_node *parent_elem(const turdom_node *n) {
    const turdom_node *p = turdom_parent(n);
    return is_elem(p) ? p : NULL;
}

static const turdom_node *prev_elem(const turdom_node *n) {
    for (n = turdom_prev_sibling(n); n; n = turdom_prev_sibling(n))
        if (is_elem(n)) return n;
    return NULL;
}

static const turdom_node *next_elem(const turdom_node *n) {
    for (n = turdom_next_sibling(n); n; n = turdom_next_sibling(n))
        if (is_elem(n)) return n;
    return NULL;
}

static int html_doc(const turdom_node *n) {
    turdom_doc *d = turdom_node_doc(n);
    return d && turdom_doc_flavor(d) == TURDOM_FLAVOR_HTML;
}

/* An element whose names compare ASCII case-insensitively: an HTML-namespace
 * element in an HTML document. */
static int html_elem(const turdom_node *n) {
    if (!html_doc(n)) return 0;
    const char *u = turdom_ns_uri(n);
    return u == NULL || strcmp(u, XHTML_NS) == 0;
}

static const char *elem_local(const turdom_node *n) {
    const char *l = turdom_local_name(n);
    return l ? l : turdom_name(n);
}

static int str_eq(const char *a, const char *b, int fold) {
    if (!a || !b) return 0;
    return fold ? ieq(a, b) : strcmp(a, b) == 0;
}

static int ns_ok(int mode, const char *want, const char *have) {
    if (mode == NS_ANY) return 1;
    if (mode == NS_NONE) return have == NULL || have[0] == '\0';
    return have && strcmp(have, want) == 0;
}

/* HTML attributes whose values selectors compare ASCII case-insensitively
 * (Selectors 4, 6.3, and the HTML standard's list). */
static const char *const ci_attrs[] = {
    "accept", "accept-charset", "align", "alink", "axis", "bgcolor", "charset",
    "checked", "clear", "codetype", "color", "compact", "declare", "defer",
    "dir", "direction", "disabled", "enctype", "face", "frame", "hreflang",
    "http-equiv", "lang", "language", "link", "media", "method", "multiple",
    "nohref", "noresize", "noshade", "nowrap", "readonly", "rel", "rev",
    "rules", "scope", "scrolling", "selected", "shape", "target", "text",
    "type", "valign", "valuetype", "vlink", NULL
};

static int is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static int ncmp(const char *a, const char *b, size_t n, int fold) {
    for (size_t i = 0; i < n; i++) {
        int x = (unsigned char)a[i], y = (unsigned char)b[i];
        if (fold) { x = ascii_lower_c(x); y = ascii_lower_c(y); }
        if (x != y) return 0;
    }
    return 1;
}

static int value_matches(int op, const char *have, const char *want, int fold) {
    size_t hn = strlen(have), wn = strlen(want);
    switch (op) {
    case A_EXISTS: return 1;
    case A_EQ: return hn == wn && ncmp(have, want, wn, fold);
    case A_INCLUDES: {
        if (wn == 0) return 0;
        for (size_t i = 0; i < wn; i++) if (is_space((unsigned char)want[i])) return 0;
        size_t i = 0;
        while (i < hn) {
            while (i < hn && is_space((unsigned char)have[i])) i++;
            size_t s = i;
            while (i < hn && !is_space((unsigned char)have[i])) i++;
            if (i - s == wn && ncmp(have + s, want, wn, fold)) return 1;
        }
        return 0;
    }
    case A_DASH:
        return (hn == wn && ncmp(have, want, wn, fold)) ||
               (hn > wn && have[wn] == '-' && ncmp(have, want, wn, fold));
    case A_PREFIX: return wn > 0 && hn >= wn && ncmp(have, want, wn, fold);
    case A_SUFFIX: return wn > 0 && hn >= wn && ncmp(have + hn - wn, want, wn, fold);
    case A_SUBSTR:
        if (wn == 0 || hn < wn) return 0;
        for (size_t i = 0; i + wn <= hn; i++)
            if (ncmp(have + i, want, wn, fold)) return 1;
        return 0;
    }
    return 0;
}

static int attr_matches(const turdom_node *n, const sel_simple *s) {
    int fold_name = html_elem(n);
    int64_t na = turdom_attr_count(n);
    for (int64_t i = 0; i < na; i++) {
        const char *qn = turdom_attr_name_at(n, i);
        const char *ln = turdom_attr_local_at(n, i);
        const char *au = turdom_attr_ns_uri_at(n, i);
        if (s->qualified) {
            if (!str_eq(qn, s->name, fold_name)) continue;
        } else {
            if (!str_eq(ln ? ln : qn, s->name, fold_name)) continue;
        }
        if (!ns_ok(s->ns_mode, s->ns_uri, au)) continue;
        if (s->op == A_EXISTS) return 1;
        int fold = s->flag == 'i' ||
                   (s->flag == 0 && fold_name && (au == NULL || au[0] == '\0') &&
                    in_list_ci(s->name, ci_attrs));
        const char *v = turdom_attr_value_at(n, i);
        if (value_matches(s->op, v ? v : "", s->value, fold)) return 1;
        /* An element has at most one attribute of a given name, except under
         * `*|`, where another namespace's may still match. */
        if (s->ns_mode != NS_ANY) return 0;
    }
    return 0;
}

/* The no-namespace attribute `name`, as #id and .class read it. */
static const char *plain_attr(const turdom_node *n, const char *name) {
    int fold = html_elem(n);
    int64_t na = turdom_attr_count(n);
    for (int64_t i = 0; i < na; i++) {
        const char *au = turdom_attr_ns_uri_at(n, i);
        if (au && au[0]) continue;
        const char *ln = turdom_attr_local_at(n, i);
        if (str_eq(ln ? ln : turdom_attr_name_at(n, i), name, fold))
            return turdom_attr_value_at(n, i);
    }
    return NULL;
}

static int same_type(const turdom_node *a, const turdom_node *b) {
    const char *ua = turdom_ns_uri(a), *ub = turdom_ns_uri(b);
    if ((ua == NULL) != (ub == NULL)) return 0;
    if (ua && strcmp(ua, ub) != 0) return 0;
    return str_eq(elem_local(a), elem_local(b), html_elem(a));
}

static int match_list(M *m, const sel_list *l, const turdom_node *n);

static int nth_ok(int64_t a, int64_t b, int64_t i) {
    if (a == 0) return i == b;
    int64_t d = i - b;
    if (a > 0) return d >= 0 && d % a == 0;
    return d <= 0 && (-d) % (-a) == 0;
}

static int nth_matches(M *m, const sel_simple *s, const turdom_node *n) {
    if (s->sub && !match_list(m, s->sub, n)) return 0;
    int64_t i = 1;
    for (const turdom_node *x = s->nth_last ? next_elem(n) : prev_elem(n); x;
         x = s->nth_last ? next_elem(x) : prev_elem(x)) {
        if (s->nth_type && !same_type(x, n)) continue;
        if (s->sub && !match_list(m, s->sub, x)) continue;
        i++;
    }
    return nth_ok(s->a, s->b, i);
}

static int is_root(const turdom_node *n) {
    const turdom_node *p = turdom_parent(n);
    return p && turdom_kind(p) == TURDOM_DOCUMENT;
}

static int is_empty(const turdom_node *n) {
    for (const turdom_node *c = turdom_first_child(n); c; c = turdom_next_sibling(c)) {
        int k = turdom_kind(c);
        if (k == TURDOM_ELEMENT) return 0;
        if ((k == TURDOM_TEXT || k == TURDOM_CDATA) && turdom_value_len(c) > 0) return 0;
    }
    return 1;
}

static int has_matches(M *m, const sel_list *l, const turdom_node *n);

static int simple_matches(M *m, const sel_simple *s, const turdom_node *n) {
    switch (s->kind) {
    case S_TYPE:
        if (!ns_ok(s->ns_mode, s->ns_uri, turdom_ns_uri(n))) return 0;
        return str_eq(s->qualified ? turdom_name(n) : elem_local(n), s->name, html_elem(n));
    case S_UNIVERSAL:
        return ns_ok(s->ns_mode, s->ns_uri, turdom_ns_uri(n));
    case S_ID: {
        const char *v = plain_attr(n, "id");
        return v && strcmp(v, s->name) == 0;
    }
    case S_CLASS: {
        const char *v = plain_attr(n, "class");
        return v && value_matches(A_INCLUDES, v, s->name, 0);
    }
    case S_ATTR: return attr_matches(n, s);
    case S_ROOT: return is_root(n);
    case S_SCOPE:
        if (!m->scope || turdom_kind(m->scope) == TURDOM_DOCUMENT) return is_root(n);
        return n == m->scope;
    case S_EMPTY: return is_empty(n);
    case S_NTH: return nth_matches(m, s, n);
    case S_NOT: return !match_list(m, s->sub, n);
    case S_IS: return match_list(m, s->sub, n);
    case S_HAS: return has_matches(m, s->sub, n);
    case S_ANCHOR: return n == m->anchor;
    }
    return 0;
}

static int compound_matches(M *m, const sel_compound *c, const turdom_node *n) {
    for (int i = 0; i < c->n; i++)
        if (!simple_matches(m, &c->s[i], n)) return 0;
    return 1;
}

/* Right to left from compound `idx`, with the early-out results of WebKit's
 * SelectorChecker: a descendant search that reaches the top fails the whole
 * selector, and a sibling search that runs out fails every sibling. */
static int match_from(M *m, const sel_complex *x, int idx, const turdom_node *n) {
    const sel_compound *c = &x->c[idx];
    if (!compound_matches(m, c, n)) return M_LOCAL;
    if (idx == 0) return M_MATCH;
    switch (c->comb) {
    case C_DESC:
        for (const turdom_node *p = parent_elem(n); p; p = parent_elem(p)) {
            int r = match_from(m, x, idx - 1, p);
            if (r == M_MATCH || r == M_COMPLETELY) return r;
        }
        return M_COMPLETELY;
    case C_CHILD: {
        const turdom_node *p = parent_elem(n);
        if (!p) return M_COMPLETELY;
        return match_from(m, x, idx - 1, p);
    }
    case C_ADJ: {
        const turdom_node *p = prev_elem(n);
        if (!p) return M_ALL_SIBLINGS;
        return match_from(m, x, idx - 1, p);
    }
    case C_SIB:
        for (const turdom_node *p = prev_elem(n); p; p = prev_elem(p)) {
            int r = match_from(m, x, idx - 1, p);
            if (r == M_MATCH || r == M_ALL_SIBLINGS || r == M_COMPLETELY) return r;
        }
        return M_ALL_SIBLINGS;
    }
    return M_LOCAL;
}

static int match_list(M *m, const sel_list *l, const turdom_node *n) {
    for (int i = 0; i < l->n; i++)
        if (match_from(m, &l->x[i], l->x[i].n - 1, n) == M_MATCH) return 1;
    return 0;
}

/* :has(): does some candidate match a relative selector anchored at n?
 * Candidates for a selector that opens with `>` or a descendant combinator
 * are n's descendants; with `+` or `~`, n's later siblings and their
 * descendants.  Each test spends one unit of the budget; when it runs out the
 * :has() answers false and the selector records it. */
static int has_matches(M *m, const sel_list *l, const turdom_node *n) {
    M inner = *m;
    inner.anchor = n;
    for (int i = 0; i < l->n; i++) {
        const sel_complex *x = &l->x[i];
        int sibling = x->n > 1 && (x->c[1].comb == C_ADJ || x->c[1].comb == C_SIB);
        /* Does a later compound go down a level?  Then candidates can sit
         * under n's later siblings, too. */
        int any_desc = 0;
        for (int k = 2; k < x->n; k++)
            if (x->c[k].comb == C_DESC || x->c[k].comb == C_CHILD) any_desc = 1;
        if (!sibling) {
            for (const turdom_node *c = turdom_next_in(n, NULL); c; c = turdom_next_in(n, c)) {
                if (!is_elem(c)) continue;
                if (m->S->budget-- <= 0) { m->S->budget_hit = 1; return 0; }
                if (match_from(&inner, x, x->n - 1, c) == M_MATCH) return 1;
            }
        } else {
            for (const turdom_node *s = next_elem(n); s; s = next_elem(s)) {
                if (m->S->budget-- <= 0) { m->S->budget_hit = 1; return 0; }
                if (match_from(&inner, x, x->n - 1, s) == M_MATCH) return 1;
                if (!any_desc) continue;
                for (const turdom_node *c = turdom_next_in(s, NULL); c; c = turdom_next_in(s, c)) {
                    if (!is_elem(c)) continue;
                    if (m->S->budget-- <= 0) { m->S->budget_hit = 1; return 0; }
                    if (match_from(&inner, x, x->n - 1, c) == M_MATCH) return 1;
                }
            }
        }
    }
    return 0;
}

static int matches_top(turdom_selector *s, const turdom_node *n, const turdom_node *scope) {
    if (!s || !s->list || !is_elem(n)) return 0;
    M m = { s, scope, NULL };
    return match_list(&m, s->list, n);
}

int turdom_selector_matches(turdom_selector *s, const turdom_node *n,
                            const turdom_node *scope) {
    return matches_top(s, n, scope);
}

void turdom_selector_reset(turdom_selector *s) {
    if (!s) return;
    s->budget = SEL_HAS_BUDGET;
    s->budget_hit = 0;
}

turdom_node *turdom_select_next(turdom_selector *s, const turdom_node *root,
                                const turdom_node *from) {
    turdom_node *n = turdom_next_in(root, from);
    while (n && !matches_top(s, n, root)) n = turdom_next_in(root, n);
    return n;
}

turdom_node *turdom_select_prev(turdom_selector *s, const turdom_node *root,
                                const turdom_node *from) {
    turdom_node *n = turdom_prev_in(root, from);
    while (n && !matches_top(s, n, root)) n = turdom_prev_in(root, n);
    return n;
}

turdom_node *turdom_closest(turdom_selector *s, const turdom_node *n) {
    for (const turdom_node *x = n; is_elem(x); x = turdom_parent(x))
        if (matches_top(s, x, n)) return (turdom_node *)x;
    return NULL;
}

/* ---- node-css-path -------------------------------------------------------- */

typedef struct { char *s; size_t n, cap; int bad; } obuf2;

static void ob2_put(obuf2 *b, const char *s, size_t n) {
    if (b->bad) return;
    if (b->n + n + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 64;
        while (ncap < b->n + n + 1) ncap *= 2;
        char *ns = (char *)realloc(b->s, ncap);
        if (!ns) { b->bad = 1; return; }
        b->s = ns;
        b->cap = ncap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

static void ob2_puts(obuf2 *b, const char *s) { ob2_put(b, s, strlen(s)); }

/* CSSOM's "serialize an identifier". */
static void put_ident(obuf2 *b, const char *s) {
    size_t n = strlen(s);
    if (n == 1 && s[0] == '-') { ob2_puts(b, "\\-"); return; }
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        char tmp[16];
        if (c == 0) {
            ob2_puts(b, "\xEF\xBF\xBD");
        } else if ((c >= 0x01 && c <= 0x1F) || c == 0x7F ||
                   (i == 0 && c >= '0' && c <= '9') ||
                   (i == 1 && c >= '0' && c <= '9' && s[0] == '-')) {
            snprintf(tmp, sizeof tmp, "\\%x ", c);
            ob2_puts(b, tmp);
        } else if (c >= 0x80 || c == '-' || c == '_' || (c >= '0' && c <= '9') ||
                   (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            ob2_put(b, (const char *)&s[i], 1);
        } else {
            tmp[0] = '\\';
            tmp[1] = (char)c;
            ob2_put(b, tmp, 2);
        }
    }
}

/* The top of n's tree: the document node, or the root of a detached subtree. */
static const turdom_node *tree_top(const turdom_node *n) {
    while (turdom_parent(n)) n = turdom_parent(n);
    return n;
}

/* Is `id` carried by exactly one element under top? */
static int id_unique(const turdom_node *top, const turdom_node *self, const char *id) {
    int count = 0;
    if (is_elem(top)) {
        const char *v = plain_attr(top, "id");
        if (v && strcmp(v, id) == 0) count++;
    }
    for (const turdom_node *c = turdom_next_in(top, NULL); c; c = turdom_next_in(top, c)) {
        if (!is_elem(c)) continue;
        const char *v = plain_attr(c, "id");
        if (v && strcmp(v, id) == 0 && ++count > 1) return 0;
    }
    (void)self;
    return count == 1;
}

char *turdom_css_path(const turdom_node *n, size_t *len) {
    if (len) *len = 0;
    if (!is_elem(n)) return NULL;
    const turdom_node *top = tree_top(n);
    /* Steps are collected leaf first, then written in reverse. */
    size_t nsteps = 0, cap = 16;
    char **steps = (char **)malloc(cap * sizeof(char *));
    if (!steps) return NULL;
    int ok = 1;
    for (const turdom_node *x = n; is_elem(x) && ok; x = turdom_parent(x)) {
        obuf2 st = {0};
        const char *id = plain_attr(x, "id");
        int stop = 0;
        if (id && id[0] && id_unique(top, x, id)) {
            ob2_puts(&st, "#");
            put_ident(&st, id);
            stop = 1;
        } else {
            put_ident(&st, elem_local(x));
            const turdom_node *p = turdom_parent(x);
            if (p) {
                int64_t k = 1;
                for (const turdom_node *s = prev_elem(x); s; s = prev_elem(s)) k++;
                char tmp[48];
                snprintf(tmp, sizeof tmp, ":nth-child(%lld)", (long long)k);
                ob2_puts(&st, tmp);
                if (turdom_kind(p) == TURDOM_DOCUMENT) { ob2_puts(&st, ":root"); stop = 1; }
            } else {
                stop = 1;
            }
        }
        if (st.bad || !st.s) { free(st.s); ok = 0; break; }
        if (nsteps == cap) {
            char **ns = (char **)realloc(steps, cap * 2 * sizeof(char *));
            if (!ns) { free(st.s); ok = 0; break; }
            steps = ns;
            cap *= 2;
        }
        steps[nsteps++] = st.s;
        if (stop) break;
    }
    obuf2 out = {0};
    for (size_t i = nsteps; ok && i-- > 0;) {
        ob2_puts(&out, steps[i]);
        if (i) ob2_puts(&out, " > ");
    }
    for (size_t i = 0; i < nsteps; i++) free(steps[i]);
    free(steps);
    if (!ok || out.bad) { free(out.s); return NULL; }
    if (len) *len = out.n;
    return out.s;
}
