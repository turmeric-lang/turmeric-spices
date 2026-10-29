/* c/dom/turmarkup.c -- the runtime half of markup literals (`#html{...}`,
 * `#xml{...}`): escaping, the URL scheme check, the template renderer's
 * pieces and the per-literal check cache.  See src/dom/holes.tur and
 * turmeric docs/archive/xml-html-followups-plan.md, follow-up A.
 *
 * A template is the string the macro (src/dom/markupc.tur) produces: one mode
 * byte ('F' an HTML fragment, 'D' a whole HTML document, 'X' XML), then the
 * literal's static markup with a marker where each hole was.  A marker is
 * \x01, a kind byte and \x02:
 *
 *   \x01c\x02        a child hole (in content)
 *   \x01a\x02        a hole inside a quoted attribute value
 *   \x01wNAME\x02    a hole that is an attribute's whole value; the static
 *                    text no longer holds ` NAME="..."`, the renderer writes it
 *   \x01kNAME\x02    a code hole (plan A6) inside the quoted value of the
 *                    attribute NAME (`on*`, `style`): its value is already
 *                    escaped for JavaScript or CSS and is attribute-escaped
 *   \x01rTAG\x02     a code hole in the raw text of <TAG> (`script`, `style`):
 *                    its value is already escaped and is written as it is,
 *                    once checked that it cannot end the element
 *
 * Holes appear in the template in the order their values are passed. */

#include "turdom.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <pthread.h>
#endif

/* ---- a growable buffer ------------------------------------------------------- */

typedef struct turdom_mbuf {
    char *s;
    size_t n, cap;
} turdom_mbuf;

turdom_mbuf *turdom_mbuf_new(void) {
    turdom_mbuf *b = (turdom_mbuf *)calloc(1, sizeof(turdom_mbuf));
    return b;
}

static void mb_put(turdom_mbuf *b, const char *s, size_t n) {
    if (!b || !n) return;
    if (b->n + n + 1 > b->cap) {
        size_t cap = b->cap ? b->cap * 2 : 256;
        while (cap < b->n + n + 1) cap *= 2;
        char *ns = (char *)realloc(b->s, cap);
        if (!ns) abort();
        b->s = ns;
        b->cap = cap;
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
    b->s[b->n] = '\0';
}

void turdom_mbuf_put(turdom_mbuf *b, const char *s) {
    if (s) mb_put(b, s, strlen(s));
}

/* The text, NUL-terminated, malloc'd; the buffer is freed. */
char *turdom_mbuf_finish(turdom_mbuf *b) {
    if (!b) return NULL;
    char *s = b->s;
    if (!s) {
        s = (char *)malloc(1);
        if (s) s[0] = '\0';
    }
    free(b);
    return s;
}

/* Text and attribute escaping, as dom-core's serializer does it: & < > always
 * (< > in attributes too, as current browsers write them), " in attributes,
 * \r everywhere; in HTML U+00A0 as &nbsp;, in XML attribute \t \n as
 * character references so attribute normalization cannot fold them. */
void turdom_mbuf_put_escaped(turdom_mbuf *b, const char *s, int attr, int html) {
    if (!s) return;
    size_t n = strlen(s), run = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        const char *rep = NULL;
        size_t skip = 0;
        switch (c) {
        case '&': rep = "&amp;"; break;
        case '<': rep = "&lt;"; break;
        case '>': rep = "&gt;"; break;
        case '"': if (attr) rep = "&quot;"; break;
        case '\'': if (attr) rep = "&#39;"; break;
        case '\t': if (attr && !html) rep = "&#9;"; break;
        case '\n': if (attr && !html) rep = "&#10;"; break;
        case '\r': rep = "&#13;"; break;
        case 0xC2:
            if (html && i + 1 < n && (unsigned char)s[i + 1] == 0xA0) { rep = "&nbsp;"; skip = 1; }
            break;
        default: break;
        }
        if (rep) {
            mb_put(b, s + run, i - run);
            mb_put(b, rep, strlen(rep));
            i += skip;
            run = i + 1;
        }
    }
    mb_put(b, s + run, n - run);
}

/* ---- the URL check (plan A-1) ------------------------------------------------- */

#define TUR_UNSAFE_URL "about:invalid#tur-unsafe"

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Is this URL safe to put in a URL-bearing attribute?  Leading C0 controls
 * and spaces are stripped and ASCII tab/LF/CR dropped anywhere, as the URL
 * parser does; then a value with no scheme (relative) or with one of http,
 * https, mailto, tel passes.  An allowlist: a denylist misses the next
 * scheme. */
int turdom_url_ok(const char *u, size_t n) {
    char scheme[16];
    size_t k = 0, i = 0;
    while (i < n && (unsigned char)u[i] <= 0x20) i++;
    for (; i < n; i++) {
        unsigned char c = (unsigned char)u[i];
        if (c == '\t' || c == '\n' || c == '\r') continue;
        if (c == ':') {
            if (k == 0) return 1;  /* ":x" -- relative per the URL parser */
            scheme[k] = '\0';
            return strcmp(scheme, "http") == 0 || strcmp(scheme, "https") == 0 ||
                   strcmp(scheme, "mailto") == 0 || strcmp(scheme, "tel") == 0;
        }
        if (c == '/' || c == '?' || c == '#') return 1;  /* the path starts first */
        int alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        int scheme_char = alpha || (k > 0 && ((c >= '0' && c <= '9') || c == '+' ||
                                             c == '-' || c == '.'));
        if (!scheme_char) return 1;  /* not a scheme: relative */
        if (k + 1 >= sizeof scheme) return 1;  /* longer than any scheme we allow */
        scheme[k++] = (char)lower(c);
    }
    return 1;
}

/* The value to write for a URL-bearing attribute: u, or the dead URL. */
const char *turdom_url_safe(const char *u) {
    if (!u) return "";
    return turdom_url_ok(u, strlen(u)) ? u : TUR_UNSAFE_URL;
}

/* srcset: each comma-separated candidate's URL checked; an unsafe one is
 * replaced by the dead URL, its descriptor kept.  malloc'd. */
char *turdom_srcset_safe(const char *s) {
    turdom_mbuf *b = turdom_mbuf_new();
    if (!s) return turdom_mbuf_finish(b);
    size_t n = strlen(s), i = 0;
    while (i < n) {
        size_t ws = i;
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' ||
                         s[i] == '\f' || s[i] == ','))
            i++;
        mb_put(b, s + ws, i - ws);
        size_t us = i;
        while (i < n && !(s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' ||
                          s[i] == '\f'))
            i++;
        /* A URL ending in commas: the commas are separators. */
        size_t ue = i;
        while (ue > us && s[ue - 1] == ',') ue--;
        if (ue > us) {
            if (turdom_url_ok(s + us, ue - us)) mb_put(b, s + us, ue - us);
            else turdom_mbuf_put(b, TUR_UNSAFE_URL);
        }
        mb_put(b, s + ue, i - ue);
        size_t ds = i;
        while (i < n && s[i] != ',') i++;
        mb_put(b, s + ds, i - ds);
    }
    return turdom_mbuf_finish(b);
}

/* ---- code holes: the inner escapers (plan A6) ------------------------------------------ */

static const char *nth_marker(const char *tpl, int64_t i);

/* The code point of the UTF-8 sequence at s (n bytes left), and its length;
 * an invalid byte is U+FFFD, length 1. */
static uint32_t utf8_at(const unsigned char *s, size_t n, size_t *len) {
    unsigned c = s[0];
    if (c < 0x80) { *len = 1; return c; }
    size_t k = c >= 0xF0 && c < 0xF5 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
    if (k == 0 || k > n) { *len = 1; return 0xFFFD; }
    uint32_t v = c & (k == 2 ? 0x1F : k == 3 ? 0x0F : 0x07);
    for (size_t i = 1; i < k; i++) {
        if ((s[i] & 0xC0) != 0x80) { *len = 1; return 0xFFFD; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *len = k;
    return v;
}

/* Text for the inside of a JavaScript (or JSON) string literal, either
 * quote: backslash, both quotes, backtick, < > & =, every C0 control, DEL and
 * the line terminators U+2028 / U+2029 become \uXXXX.  Escaping < is what
 * keeps </script and <!-- out of a <script> body.  malloc'd. */
char *turdom_js_string_escape(const char *s) {
    turdom_mbuf *b = turdom_mbuf_new();
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    size_t n = strlen((const char *)p), i = 0;
    char tmp[8];
    while (i < n) {
        size_t len;
        uint32_t c = utf8_at(p + i, n - i, &len);
        if (c < 0x20 || c == 0x7F || c == '\\' || c == '"' || c == '\'' || c == '`' ||
            c == '<' || c == '>' || c == '&' || c == '=' || c == 0x2028 || c == 0x2029) {
            snprintf(tmp, sizeof tmp, "\\u%04x", (unsigned)c);
            turdom_mbuf_put(b, tmp);
        } else if (c == 0xFFFD && len == 1) {
            turdom_mbuf_put(b, "\\ufffd");
        } else {
            mb_put(b, (const char *)p + i, len);
        }
        i += len;
    }
    return turdom_mbuf_finish(b);
}

/* A double-quoted JavaScript (and JSON) string literal.  malloc'd. */
char *turdom_js_quote(const char *s) {
    char *e = turdom_js_string_escape(s);
    turdom_mbuf *b = turdom_mbuf_new();
    turdom_mbuf_put(b, "\"");
    turdom_mbuf_put(b, e);
    turdom_mbuf_put(b, "\"");
    free(e);
    return turdom_mbuf_finish(b);
}

/* A JavaScript value in expression position, padded with a space on each
 * side, so `a-${x}` with x = -1 cannot form `--`.  malloc'd. */
char *turdom_js_pad(const char *s) {
    turdom_mbuf *b = turdom_mbuf_new();
    turdom_mbuf_put(b, " ");
    turdom_mbuf_put(b, s ? s : "");
    turdom_mbuf_put(b, " ");
    return turdom_mbuf_finish(b);
}

#define TUR_UNSAFE_CSS "tur-unsafe"

/* A CSS property value from text: kept when every byte is an ASCII letter or
 * digit or one of # . - _ % , and space; otherwise the invalid value
 * tur-unsafe, which the browser drops (parens, quotes, \, /, ;, {, }, < --
 * so expression(, url( and comments -- never get through). */
const char *turdom_css_value(const char *s) {
    if (!s) return "";
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned c = *p;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                 c == '#' || c == '.' || c == '-' || c == '_' || c == '%' || c == ',' || c == ' ';
        if (!ok) return TUR_UNSAFE_CSS;
    }
    return s;
}

/* Text for the inside of a quoted CSS string: everything but an ASCII letter
 * or digit becomes a \HEX escape followed by a space (< gives \3c ).
 * malloc'd. */
char *turdom_css_string_escape(const char *s) {
    turdom_mbuf *b = turdom_mbuf_new();
    const unsigned char *p = (const unsigned char *)(s ? s : "");
    size_t n = strlen((const char *)p), i = 0;
    char tmp[16];
    while (i < n) {
        size_t len;
        uint32_t c = utf8_at(p + i, n - i, &len);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            mb_put(b, (const char *)p + i, 1);
        } else {
            snprintf(tmp, sizeof tmp, "\\%x ", (unsigned)(c == 0 ? 0xFFFD : c));
            turdom_mbuf_put(b, tmp);
        }
        i += len;
    }
    return turdom_mbuf_finish(b);
}

/* A URL for the inside of url(...), quoted or not, already through the
 * scheme check: parens, both quotes, whitespace, \ and < > percent-encoded.
 * malloc'd. */
char *turdom_css_url_escape(const char *s) {
    turdom_mbuf *b = turdom_mbuf_new();
    char tmp[4];
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; p++) {
        unsigned c = *p;
        if (c == '(' || c == ')' || c == '\'' || c == '"' || c == '\\' || c == '<' ||
            c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c < 0x20 || c == 0x7F) {
            snprintf(tmp, sizeof tmp, "%%%02X", c);
            turdom_mbuf_put(b, tmp);
        } else {
            mb_put(b, (const char *)p, 1);
        }
    }
    return turdom_mbuf_finish(b);
}

/* May value s go in code hole i of tpl as it is?  Only an 'r' hole has
 * anything to check: s must not hold its element's end tag `</TAG` (any
 * case), nor -- in a <script> -- `<!--`, which moves the HTML tokenizer into
 * its escaped script states.  The escapers never produce either; a
 * TrustedJs / TrustedCss value can. */
int turdom_code_ok(const char *tpl, int64_t i, const char *s) {
    const char *m = nth_marker(tpl, i);
    if (!m || m[1] != 'r' || !s) return 1;
    const char *t = m + 2, *e = strchr(t, '\x02');
    size_t tn = e ? (size_t)(e - t) : strlen(t);
    for (const char *p = s; *p; p++) {
        if (p[0] != '<') continue;
        if (p[1] == '/') {
            size_t j = 0;
            while (j < tn && p[2 + j] && lower((unsigned char)p[2 + j]) == lower((unsigned char)t[j])) j++;
            if (j == tn) return 0;
        }
        if (tn == 6 && strncmp(p, "<!--", 4) == 0) return 0;
    }
    return 1;
}

/* ---- walking a template ----------------------------------------------------------- */

/* Append the static text of tpl from pos up to the next marker; return the
 * marker's offset, or -1 when the template ends first.  pos 0 skips the mode
 * byte. */
int64_t turdom_tpl_static(turdom_mbuf *b, const char *tpl, int64_t pos) {
    if (pos == 0 && tpl[0]) pos = 1;
    const char *m = strchr(tpl + pos, '\x01');
    if (!m) {
        turdom_mbuf_put(b, tpl + pos);
        return -1;
    }
    mb_put(b, tpl + pos, (size_t)(m - (tpl + pos)));
    return (int64_t)(m - tpl);
}

int turdom_tpl_kind(const char *tpl, int64_t pos) {
    return (unsigned char)tpl[pos + 1];
}

int64_t turdom_tpl_after(const char *tpl, int64_t pos) {
    const char *e = strchr(tpl + pos, '\x02');
    return e ? (int64_t)(e - tpl) + 1 : (int64_t)strlen(tpl);
}

/* A whole-value attribute hole: ` NAME="v"` for text, ` NAME` (HTML) or
 * ` NAME="NAME"` (XML) for on, nothing for off. */
void turdom_tpl_whole_attr(turdom_mbuf *b, const char *tpl, int64_t pos,
                           int state, const char *v, int html) {
    if (state == 0) return;
    const char *name = tpl + pos + 2;
    const char *end = strchr(name, '\x02');
    size_t nl = end ? (size_t)(end - name) : strlen(name);
    mb_put(b, " ", 1);
    mb_put(b, name, nl);
    if (state == 1) {
        if (!html) {
            mb_put(b, "=\"", 2);
            mb_put(b, name, nl);
            mb_put(b, "\"", 1);
        }
        return;
    }
    mb_put(b, "=\"", 2);
    turdom_mbuf_put_escaped(b, v, 1, html);
    mb_put(b, "\"", 1);
}

/* ---- checking a literal against the real parser -------------------------------------- */

#define HOLE_PREFIX "__tur_hole_"

/* The template as markup the parser can read: each child marker becomes the
 * comment <!--__tur_hole_N__-->, each part marker the text __tur_hole_N__ and
 * each whole marker ` NAME="__tur_hole_N__"`.  XML wraps it in a root
 * element.  malloc'd. */
char *turdom_tpl_check_markup(const char *tpl) {
    turdom_mbuf *b = turdom_mbuf_new();
    int xml = tpl[0] == 'X';
    if (xml) turdom_mbuf_put(b, "<__tur_root__>");
    int64_t pos = 0, k = 0;
    char tmp[64];
    for (;;) {
        pos = turdom_tpl_static(b, tpl, pos);
        if (pos < 0) break;
        int kind = turdom_tpl_kind(tpl, pos);
        if (kind == 'c') {
            snprintf(tmp, sizeof tmp, "<!--" HOLE_PREFIX "%lld__-->", (long long)k);
            turdom_mbuf_put(b, tmp);
        } else if (kind == 'a' || kind == 'k' || kind == 'r') {
            snprintf(tmp, sizeof tmp, HOLE_PREFIX "%lld__", (long long)k);
            turdom_mbuf_put(b, tmp);
        } else {
            snprintf(tmp, sizeof tmp, HOLE_PREFIX "%lld__", (long long)k);
            turdom_tpl_whole_attr(b, tpl, pos, 2, tmp, 1);
        }
        k++;
        pos = turdom_tpl_after(tpl, pos);
    }
    if (xml) turdom_mbuf_put(b, "</__tur_root__>");
    return turdom_mbuf_finish(b);
}

/* The hole kinds of a template, in order ('c', 'a', 'w', 'k' or 'r'), and
 * the name a 'w' or 'k' hole's attribute, or an 'r' hole's element, has. */
int64_t turdom_tpl_hole_count(const char *tpl) {
    int64_t k = 0;
    for (const char *p = tpl; (p = strchr(p, '\x01')); p++) k++;
    return k;
}

static const char *nth_marker(const char *tpl, int64_t i) {
    const char *p = tpl;
    for (;;) {
        p = strchr(p, '\x01');
        if (!p) return NULL;
        if (i-- == 0) return p;
        p++;
    }
}

int turdom_tpl_hole_kind(const char *tpl, int64_t i) {
    const char *m = nth_marker(tpl, i);
    return m ? (unsigned char)m[1] : 0;
}

/* Does hole i (a 'w' or 'k' hole: an attribute; an 'r' hole: an element)
 * name `name`?  ASCII case-insensitive in HTML, where the parser lower-cases
 * names. */
int turdom_tpl_hole_attr_is(const char *tpl, int64_t i, const char *name) {
    const char *m = nth_marker(tpl, i);
    if (!m || (m[1] != 'w' && m[1] != 'k' && m[1] != 'r') || !name) return 0;
    const char *a = m + 2, *e = strchr(a, '\x02');
    size_t n = e ? (size_t)(e - a) : strlen(a);
    if (strlen(name) != n) return 0;
    int fold = tpl[0] != 'X';
    for (size_t j = 0; j < n; j++) {
        int x = (unsigned char)a[j], y = (unsigned char)name[j];
        if (fold) { x = lower(x); y = lower(y); }
        if (x != y) return 0;
    }
    return 1;
}

/* Every __tur_hole_N__ in s, as N, one per call: returns the index found at
 * or after *at and advances *at past it; -1 when there are no more. */
int64_t turdom_tpl_next_hole_ref(const char *s, int64_t *at) {
    if (!s) return -1;
    const char *p = strstr(s + *at, HOLE_PREFIX);
    while (p) {
        const char *d = p + strlen(HOLE_PREFIX);
        int64_t v = 0;
        int nd = 0;
        while (*d >= '0' && *d <= '9') { v = v * 10 + (*d - '0'); d++; nd++; }
        if (nd && d[0] == '_' && d[1] == '_') {
            *at = (int64_t)(d + 2 - s);
            return v;
        }
        p = strstr(p + 1, HOLE_PREFIX);
    }
    *at = (int64_t)strlen(s);
    return -1;
}

/* Is s exactly __tur_hole_N__? */
int turdom_tpl_is_hole_ref(const char *s, int64_t n) {
    char tmp[64];
    snprintf(tmp, sizeof tmp, HOLE_PREFIX "%lld__", (long long)n);
    return s && strcmp(s, tmp) == 0;
}

/* ---- the per-literal cache ---------------------------------------------------------------
 *
 * A literal is checked against the parser the first time it renders.  The
 * result -- and, per child hole, the element the parser put it in, which is
 * the context a raw-html value is parsed in -- is kept for the life of the
 * process, keyed by the template's address (a string literal: one per
 * literal, or one per identical literal, which is the same answer). */

typedef struct site {
    const char *tpl;
    int64_t n;
    char **parent;
    struct site *next;
} site;

static site *g_sites = NULL;
#ifndef _WIN32
static pthread_mutex_t g_sites_mu = PTHREAD_MUTEX_INITIALIZER;
#define SITES_LOCK() pthread_mutex_lock(&g_sites_mu)
#define SITES_UNLOCK() pthread_mutex_unlock(&g_sites_mu)
#else
#define SITES_LOCK() ((void)0)
#define SITES_UNLOCK() ((void)0)
#endif

static site *find_site(const char *tpl) {
    for (site *s = g_sites; s; s = s->next)
        if (s->tpl == tpl) return s;
    return NULL;
}

int turdom_tpl_checked(const char *tpl) {
    SITES_LOCK();
    int r = find_site(tpl) != NULL;
    SITES_UNLOCK();
    return r;
}

/* Record a checked literal; parents[i] is the parent element of child hole i
 * (NULL for other holes).  The strings are copied. */
void turdom_tpl_mark_checked(const char *tpl, const char *const *parents, int64_t n) {
    site *s = (site *)calloc(1, sizeof(site));
    if (!s) return;
    s->tpl = tpl;
    s->n = n;
    s->parent = (char **)calloc((size_t)(n ? n : 1), sizeof(char *));
    for (int64_t i = 0; i < n && s->parent; i++)
        if (parents[i]) {
            size_t l = strlen(parents[i]);
            s->parent[i] = (char *)malloc(l + 1);
            if (s->parent[i]) memcpy(s->parent[i], parents[i], l + 1);
        }
    SITES_LOCK();
    if (find_site(tpl)) {
        SITES_UNLOCK();
        for (int64_t i = 0; i < n; i++) free(s->parent[i]);
        free(s->parent);
        free(s);
        return;
    }
    s->next = g_sites;
    g_sites = s;
    SITES_UNLOCK();
}

/* The element child hole i of a checked literal sits in ("template" at the
 * top level of a fragment). */
const char *turdom_tpl_parent(const char *tpl, int64_t i) {
    SITES_LOCK();
    site *s = find_site(tpl);
    const char *p = (s && i >= 0 && i < s->n && s->parent[i]) ? s->parent[i] : "template";
    SITES_UNLOCK();
    return p;
}

/* ---- the check itself -------------------------------------------------------------------- */

static void add_err(turdom_mbuf *e, const char *msg, int64_t k) {
    char tmp[64];
    if (e->n) turdom_mbuf_put(e, "; ");
    if (k >= 0) {
        snprintf(tmp, sizeof tmp, "hole %lld ", (long long)k);
        turdom_mbuf_put(e, tmp);
    }
    turdom_mbuf_put(e, msg);
}

static int has_ref(const char *s) { return s && strstr(s, HOLE_PREFIX) != NULL; }

/* Check the tree the parser built from turdom_tpl_check_markup(tpl): each
 * hole marker turns up exactly once, in the kind of slot the macro predicted.
 * A disagreement is a bug in the macro's context tokenizer (or markup it
 * cannot follow), so the caller panics with the message.  On success the
 * literal is recorded as checked and NULL returned; otherwise a malloc'd
 * message. */
char *turdom_tpl_verify(const char *tpl, const turdom_node *top) {
    int64_t n = turdom_tpl_hole_count(tpl);
    int *seen = (int *)calloc((size_t)(n ? n : 1), sizeof(int));
    const char **parent = (const char **)calloc((size_t)(n ? n : 1), sizeof(char *));
    turdom_mbuf *e = turdom_mbuf_new();
    for (const turdom_node *x = turdom_next_in(top, NULL); x; x = turdom_next_in(top, x)) {
        int kind = turdom_kind(x);
        if (kind == TURDOM_COMMENT) {
            const char *v = turdom_value(x);
            if (!has_ref(v)) continue;
            int64_t at = 0, k = turdom_tpl_next_hole_ref(v, &at);
            if (k < 0 || k >= n || !turdom_tpl_is_hole_ref(v, k)) {
                add_err(e, "a hole marker turned up inside a comment", -1);
                continue;
            }
            if (turdom_tpl_hole_kind(tpl, k) != 'c')
                add_err(e, "is an attribute hole but parsed as content", k);
            seen[k]++;
            /* The parent element's name: the context a raw-html value in
             * this hole is parsed in.  None at the top level. */
            const turdom_node *p = turdom_parent(x);
            const char *pn = NULL;
            if (p && turdom_kind(p) == TURDOM_ELEMENT) {
                pn = turdom_local_name(p) ? turdom_local_name(p) : turdom_name(p);
                if (strcmp(pn, "__tur_root__") == 0) pn = NULL;
            }
            parent[k] = pn;
        } else if (kind == TURDOM_ELEMENT) {
            if (has_ref(turdom_name(x))) add_err(e, "a hole marker turned up in an element name", -1);
            int64_t na = turdom_attr_count(x);
            for (int64_t i = 0; i < na; i++) {
                const char *an = turdom_attr_name_at(x, i), *av = turdom_attr_value_at(x, i);
                if (has_ref(an)) add_err(e, "a hole marker turned up in an attribute name", -1);
                int64_t at = 0, k;
                while ((k = turdom_tpl_next_hole_ref(av, &at)) >= 0) {
                    if (k >= n) { add_err(e, "unknown hole marker in an attribute", -1); continue; }
                    int hk = turdom_tpl_hole_kind(tpl, k);
                    if (hk == 'c') add_err(e, "is a content hole but parsed inside an attribute", k);
                    else if (hk == 'r') add_err(e, "is a <script>/<style> hole but parsed inside an attribute", k);
                    else if (hk == 'w' && !(turdom_tpl_is_hole_ref(av, k) &&
                                            turdom_tpl_hole_attr_is(tpl, k, an)))
                        add_err(e, "is a whole attribute value but parsed differently", k);
                    else if (hk == 'k' && !turdom_tpl_hole_attr_is(tpl, k, an))
                        add_err(e, "is a code hole of another attribute than the one it parsed in", k);
                    seen[k]++;
                }
            }
        } else if (kind == TURDOM_TEXT && has_ref(turdom_value(x))) {
            /* Only a code hole in a <script> or <style> body may be text, and
             * only in the element the macro read it in, as HTML. */
            const turdom_node *p = turdom_parent(x);
            const char *pn = (p && turdom_kind(p) == TURDOM_ELEMENT) ? turdom_local_name(p) : NULL;
            const char *pu = p ? turdom_ns_uri(p) : NULL;
            int html_el = !pu || strcmp(pu, "http://www.w3.org/1999/xhtml") == 0;
            const char *v = turdom_value(x);
            int64_t at = 0, k;
            while ((k = turdom_tpl_next_hole_ref(v, &at)) >= 0) {
                if (k >= n) { add_err(e, "unknown hole marker in text", -1); continue; }
                if (turdom_tpl_hole_kind(tpl, k) != 'r' || !pn || !html_el ||
                    !turdom_tpl_hole_attr_is(tpl, k, pn))
                    add_err(e, "turned up as text (raw text, or markup the tokenizer misread)", k);
                seen[k]++;
            }
        } else if (has_ref(turdom_value(x)) || has_ref(turdom_name(x)) ||
                   has_ref(turdom_public_id(x)) || has_ref(turdom_system_id(x))) {
            add_err(e, kind == TURDOM_TEXT || kind == TURDOM_CDATA
                           ? "a hole marker turned up as text (raw text, or markup the tokenizer misread)"
                           : "a hole marker turned up in a doctype or processing instruction", -1);
        }
    }
    for (int64_t k = 0; k < n; k++)
        if (seen[k] != 1)
            add_err(e, seen[k] ? "turned up more than once" : "disappeared (the parser dropped or moved it)", k);
    char *msg = NULL;
    if (e->n) {
        turdom_mbuf *m = turdom_mbuf_new();
        turdom_mbuf_put(m, "markup literal does not parse the way its holes were placed: ");
        mb_put(m, e->s, e->n);
        turdom_mbuf_put(m, "\n  the literal, with its holes marked: ");
        char *cm = turdom_tpl_check_markup(tpl);
        turdom_mbuf_put(m, cm);
        free(cm);
        msg = turdom_mbuf_finish(m);
    } else {
        turdom_tpl_mark_checked(tpl, parent, n);
    }
    free(turdom_mbuf_finish(e));
    free(seen);
    free(parent);
    return msg;
}
