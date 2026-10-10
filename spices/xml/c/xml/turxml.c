/* c/xml/turxml.c -- expat glue for tur-xml: an event queue and a tree builder.
 *
 * expat is a push parser: it calls back into C while it parses a buffer.  A
 * Turmeric handler cannot be one of those callbacks (it is a closure that may
 * perform effects, and the events must be built as Turmeric values), so the
 * callbacks here only RECORD events into a queue.  The Turmeric driver then
 * drains the queue one event at a time (turxml_next + the turxml_ev_*
 * accessors), builds a SaxEvent, and calls the handler.
 *
 * Input is fed in chunks of at most TURXML_CHUNK bytes, so the queue never
 * holds more than one chunk's events: memory is O(chunk + depth), not
 * O(document), whatever the document size.
 *
 * Character data is COALESCED: expat splits text at buffer boundaries,
 * entity references and newlines, and a SAX consumer should not see where
 * the input happened to be chunked.  Text accumulates until the next
 * non-text event (or the end of input) and is then queued as one Chars
 * event.  That is what makes the push parser's events identical for any
 * chunking of the same document.
 *
 * The DOM builder (turxml_dom_run) consumes the very same queue in C, so the
 * tree is built on the SAX layer rather than beside it.
 *
 * Safe defaults, all set in turxml_new:
 *   - no external entity is ever loaded unless allow_ext is set, and even
 *     then only from the local filesystem (never a URL);
 *   - parameter entity parsing (the external DTD subset) is off;
 *   - expat's billion-laughs amplification limit is set to max_ratio;
 *   - element depth and total input bytes are capped.
 */

/* The library is built with EXPAT_GE ON (expat's default, see
 * cmake-deps/expat).  XML_GE lives in the build's expat_config.h, which a
 * consumer never sees, and expat.h only declares the billion-laughs
 * amplification API under it -- so say so before including. */
#ifndef XML_GE
#define XML_GE 1
#endif
#include <expat.h>

#include <errno.h>
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
extern turdom_node *turdom_parent(const turdom_node *n);
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
#define TURDOM_FLAVOR_XML 0

/* ---- constants shared with src/xml/sax.tur ---------------------------- */

enum {
    EV_START = 1, EV_END = 2, EV_CHARS = 3, EV_COMMENT = 4, EV_PI = 5,
    EV_START_CDATA = 6, EV_END_CDATA = 7, EV_DOCTYPE = 8
};

/* ParseErrorKind, in dom/core's constructor order. */
enum { ERR_SYNTAX = 0, ERR_LIMIT = 1, ERR_REFUSED = 2, ERR_IO = 3 };

#define TURXML_CHUNK ((size_t)64 * 1024)
#define TURXML_NS_SEP '\x01'
#define TURXML_MAX_EXT_NESTING 16

/* ---- positions: the 64-bit API arrived in 2.9.0 ---------------------- */

#if XML_MAJOR_VERSION > 2 || (XML_MAJOR_VERSION == 2 && XML_MINOR_VERSION >= 9)
#define CUR_LINE(p) ((int64_t)XML_GetCurrentLineNumber64(p))
#define CUR_COL(p)  ((int64_t)XML_GetCurrentColumnNumber64(p))
#else
#define CUR_LINE(p) ((int64_t)XML_GetCurrentLineNumber(p))
#define CUR_COL(p)  ((int64_t)XML_GetCurrentColumnNumber(p))
#endif

/* ---- events ------------------------------------------------------------ */

/* A name in its three spellings.  `sax` is what a SAX consumer sees (Clark
 * `{uri}local` under namespace processing, else the raw name); qname, uri and
 * local are what the tree stores so it can serialize the prefix back. */
typedef struct {
    char *sax;
    char *qname;
    char *uri;
    char *local;
} xname;

typedef struct {
    int kind;
    xname name;          /* START/END element; PI target and DOCTYPE name in .sax */
    char *text;          /* CHARS/COMMENT text, PI data */
    size_t text_len;
    char *pubid;         /* DOCTYPE */
    char *sysid;
    int nattrs;
    xname *attr_names;
    char **attr_values;
    int nns;             /* namespace declarations on this START (tree only) */
    char **ns_prefix;    /* NULL entry = default namespace */
    char **ns_uri;
    int64_t line;
    int64_t col;
} xev;

typedef struct turxml {
    XML_Parser p;
    int ns;
    int64_t max_depth;
    int64_t max_bytes;
    int allow_ext;
    int ext_nesting;
    char *base;

    int64_t depth;
    int64_t bytes;

    /* event queue (ring would be overkill: it is drained fully per chunk) */
    xev *q;
    size_t qhead, qlen, qcap;
    xev cur;
    int has_cur;

    /* pending, not yet queued, character data */
    char *text;
    size_t tlen, tcap;
    int64_t tline, tcol;
    int has_text;

    /* namespace declarations seen since the last start tag */
    int pns;
    int pns_cap;
    char **pns_prefix;
    char **pns_uri;

    /* input source */
    const char *src;
    size_t src_len, src_off;
    FILE *file;
    int src_set;
    int finished;
    int stopped;

    /* error */
    int failed;
    int err_kind;
    char *err_msg;
    int64_t err_line, err_col;

    /* tree building */
    turdom_doc *doc;
    turdom_node *at;
    int in_cdata;
    int cdata_has_chars;
} turxml;

static char *xstrndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static char *xstrdup(const char *s) { return s ? xstrndup(s, strlen(s)) : NULL; }

static void xname_free(xname *n) {
    free(n->sax); free(n->qname); free(n->uri); free(n->local);
    memset(n, 0, sizeof(*n));
}

static void xev_free(xev *e) {
    xname_free(&e->name);
    free(e->text);
    free(e->pubid);
    free(e->sysid);
    for (int i = 0; i < e->nattrs; i++) {
        xname_free(&e->attr_names[i]);
        free(e->attr_values[i]);
    }
    free(e->attr_names);
    free(e->attr_values);
    for (int i = 0; i < e->nns; i++) { free(e->ns_prefix[i]); free(e->ns_uri[i]); }
    free(e->ns_prefix);
    free(e->ns_uri);
    memset(e, 0, sizeof(*e));
}

static void set_error(turxml *x, int kind, const char *msg) {
    if (x->failed) return;
    x->failed = 1;
    x->err_kind = kind;
    x->err_msg = xstrdup(msg ? msg : "parse error");
    if (x->p) {
        x->err_line = CUR_LINE(x->p);
        x->err_col = CUR_COL(x->p) + 1;
    }
}

static void oom(turxml *x) {
    set_error(x, ERR_LIMIT, "out of memory");
    if (x->p) XML_StopParser(x->p, XML_FALSE);
}

static xev *push_ev(turxml *x, int kind) {
    if (x->qhead + x->qlen == x->qcap) {
        if (x->qhead > 0) {
            memmove(x->q, x->q + x->qhead, x->qlen * sizeof(xev));
            x->qhead = 0;
        } else {
            size_t cap = x->qcap ? x->qcap * 2 : 64;
            xev *nq = (xev *)realloc(x->q, cap * sizeof(xev));
            if (!nq) { oom(x); return NULL; }
            x->q = nq;
            x->qcap = cap;
        }
    }
    xev *e = &x->q[x->qhead + x->qlen++];
    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->line = CUR_LINE(x->p);
    e->col = CUR_COL(x->p) + 1;
    return e;
}

/* Split an expat name.  Under namespace processing with triplets on, expat
 * hands us "uri<SEP>local<SEP>prefix", "uri<SEP>local" (default namespace)
 * or plain "local" (no namespace). */
static int split_name(turxml *x, const char *raw, xname *out) {
    memset(out, 0, sizeof(*out));
    if (!x->ns) {
        out->sax = xstrdup(raw);
        out->qname = xstrdup(raw);
        out->local = xstrdup(raw);
        return out->sax && out->qname && out->local;
    }
    const char *s1 = strchr(raw, TURXML_NS_SEP);
    if (!s1) {
        out->sax = xstrdup(raw);
        out->qname = xstrdup(raw);
        out->local = xstrdup(raw);
        return out->sax && out->qname && out->local;
    }
    const char *loc = s1 + 1;
    const char *s2 = strchr(loc, TURXML_NS_SEP);
    size_t ulen = (size_t)(s1 - raw);
    size_t llen = s2 ? (size_t)(s2 - loc) : strlen(loc);
    out->uri = xstrndup(raw, ulen);
    out->local = xstrndup(loc, llen);
    out->sax = (char *)malloc(ulen + llen + 3);
    if (out->sax) {
        out->sax[0] = '{';
        memcpy(out->sax + 1, raw, ulen);
        out->sax[ulen + 1] = '}';
        memcpy(out->sax + ulen + 2, loc, llen);
        out->sax[ulen + llen + 2] = '\0';
    }
    if (s2) {
        const char *pre = s2 + 1;
        size_t plen = strlen(pre);
        out->qname = (char *)malloc(plen + llen + 2);
        if (out->qname) {
            memcpy(out->qname, pre, plen);
            out->qname[plen] = ':';
            memcpy(out->qname + plen + 1, loc, llen);
            out->qname[plen + llen + 1] = '\0';
        }
    } else {
        out->qname = xstrndup(loc, llen);
    }
    return out->uri && out->local && out->sax && out->qname;
}

static void flush_text(turxml *x) {
    if (!x->has_text || x->failed) return;
    xev *e = push_ev(x, EV_CHARS);
    if (!e) return;
    e->text = xstrndup(x->text ? x->text : "", x->tlen);
    e->text_len = x->tlen;
    e->line = x->tline;
    e->col = x->tcol;
    if (!e->text) oom(x);
    x->tlen = 0;
    x->has_text = 0;
}

/* ---- expat callbacks ---------------------------------------------------- */

static void XMLCALL on_start(void *ud, const XML_Char *name,
                             const XML_Char **atts) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    x->depth++;
    if (x->max_depth > 0 && x->depth > x->max_depth) {
        char msg[96];
        snprintf(msg, sizeof msg, "maximum element depth %lld exceeded",
                 (long long)x->max_depth);
        set_error(x, ERR_LIMIT, msg);
        XML_StopParser(x->p, XML_FALSE);
        return;
    }
    xev *e = push_ev(x, EV_START);
    if (!e) return;
    if (!split_name(x, name, &e->name)) { oom(x); return; }
    int n = 0;
    while (atts[2 * n]) n++;
    if (n) {
        e->attr_names = (xname *)calloc((size_t)n, sizeof(xname));
        e->attr_values = (char **)calloc((size_t)n, sizeof(char *));
        if (!e->attr_names || !e->attr_values) { oom(x); return; }
        e->nattrs = n;
        for (int i = 0; i < n; i++) {
            if (!split_name(x, atts[2 * i], &e->attr_names[i])) { oom(x); return; }
            e->attr_values[i] = xstrdup(atts[2 * i + 1]);
            if (!e->attr_values[i]) { oom(x); return; }
        }
    }
    /* Hand the pending namespace declarations to this element. */
    if (x->pns) {
        e->nns = x->pns;
        e->ns_prefix = x->pns_prefix;
        e->ns_uri = x->pns_uri;
        x->pns = 0;
        x->pns_cap = 0;
        x->pns_prefix = NULL;
        x->pns_uri = NULL;
    }
}

static void XMLCALL on_end(void *ud, const XML_Char *name) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    x->depth--;
    xev *e = push_ev(x, EV_END);
    if (!e) return;
    if (!split_name(x, name, &e->name)) oom(x);
}

static void XMLCALL on_chars(void *ud, const XML_Char *s, int len) {
    turxml *x = (turxml *)ud;
    if (x->failed || len <= 0) return;
    if (!x->has_text) {
        x->has_text = 1;
        x->tlen = 0;
        x->tline = CUR_LINE(x->p);
        x->tcol = CUR_COL(x->p) + 1;
    }
    if (x->tlen + (size_t)len + 1 > x->tcap) {
        size_t cap = x->tcap ? x->tcap : 256;
        while (cap < x->tlen + (size_t)len + 1) cap *= 2;
        char *nt = (char *)realloc(x->text, cap);
        if (!nt) { oom(x); return; }
        x->text = nt;
        x->tcap = cap;
    }
    memcpy(x->text + x->tlen, s, (size_t)len);
    x->tlen += (size_t)len;
}

static void XMLCALL on_comment(void *ud, const XML_Char *data) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    xev *e = push_ev(x, EV_COMMENT);
    if (!e) return;
    e->text = xstrdup(data);
    e->text_len = e->text ? strlen(e->text) : 0;
    if (!e->text) oom(x);
}

static void XMLCALL on_pi(void *ud, const XML_Char *target,
                          const XML_Char *data) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    xev *e = push_ev(x, EV_PI);
    if (!e) return;
    e->name.sax = xstrdup(target);
    e->text = xstrdup(data ? data : "");
    e->text_len = e->text ? strlen(e->text) : 0;
    if (!e->name.sax || !e->text) oom(x);
}

static void XMLCALL on_start_cdata(void *ud) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    push_ev(x, EV_START_CDATA);
}

static void XMLCALL on_end_cdata(void *ud) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    push_ev(x, EV_END_CDATA);
}

static void XMLCALL on_doctype(void *ud, const XML_Char *name,
                               const XML_Char *sysid, const XML_Char *pubid,
                               int has_internal_subset) {
    (void)has_internal_subset;
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    flush_text(x);
    xev *e = push_ev(x, EV_DOCTYPE);
    if (!e) return;
    e->name.sax = xstrdup(name);
    e->pubid = xstrdup(pubid);
    e->sysid = xstrdup(sysid);
    if (!e->name.sax) oom(x);
}

static void XMLCALL on_ns_start(void *ud, const XML_Char *prefix,
                                const XML_Char *uri) {
    turxml *x = (turxml *)ud;
    if (x->failed) return;
    if (x->pns == x->pns_cap) {
        int cap = x->pns_cap ? x->pns_cap * 2 : 4;
        char **np = (char **)realloc(x->pns_prefix, (size_t)cap * sizeof(char *));
        if (!np) { oom(x); return; }
        x->pns_prefix = np;
        char **nu = (char **)realloc(x->pns_uri, (size_t)cap * sizeof(char *));
        if (!nu) { oom(x); return; }
        x->pns_uri = nu;
        x->pns_cap = cap;
    }
    x->pns_prefix[x->pns] = xstrdup(prefix);      /* NULL = default namespace */
    x->pns_uri[x->pns] = xstrdup(uri ? uri : "");
    x->pns++;
}

/* An entity expat could not expand: an undeclared reference in a document
 * whose external DTD subset was not read.  Its replacement text is unknown,
 * so it contributes nothing -- the SAX2 skippedEntity behaviour. */
static void XMLCALL on_skipped(void *ud, const XML_Char *name, int is_pe) {
    (void)ud; (void)name; (void)is_pe;
}

static char *resolve_local(turxml *x, const XML_Char *base,
                           const XML_Char *sysid) {
    if (!sysid) return NULL;
    const char *s = sysid;
    if (strncmp(s, "file://", 7) == 0) {
        s += 7;
        if (strncmp(s, "localhost/", 10) == 0) s += 9;
    } else if (strstr(s, "://")) {
        return NULL;                      /* never the network */
    }
    if (s[0] == '/') return xstrdup(s);
    const char *b = base ? base : x->base;
    if (!b || !b[0]) return xstrdup(s);
    size_t bl = strlen(b), sl = strlen(s);
    int slash = b[bl - 1] == '/';
    char *out = (char *)malloc(bl + sl + 2);
    if (!out) return NULL;
    memcpy(out, b, bl);
    if (!slash) out[bl++] = '/';
    memcpy(out + bl, s, sl + 1);
    return out;
}

static int XMLCALL on_external(XML_Parser parser, const XML_Char *context,
                               const XML_Char *base, const XML_Char *sysid,
                               const XML_Char *pubid) {
    (void)pubid;
    turxml *x = (turxml *)XML_GetUserData(parser);
    if (x->failed) return XML_STATUS_ERROR;
    char msg[512];
    if (!x->allow_ext) {
        snprintf(msg, sizeof msg,
                 "external entity '%s' refused: external entities are off "
                 "(ParseOpts allow-external-entities)",
                 sysid ? sysid : "?");
        set_error(x, ERR_REFUSED, msg);
        return XML_STATUS_ERROR;
    }
    if (x->ext_nesting >= TURXML_MAX_EXT_NESTING) {
        set_error(x, ERR_LIMIT, "external entities nested too deeply");
        return XML_STATUS_ERROR;
    }
    char *path = resolve_local(x, base, sysid);
    if (!path) {
        snprintf(msg, sizeof msg,
                 "external entity '%s' refused: only local files are loaded",
                 sysid ? sysid : "?");
        set_error(x, ERR_REFUSED, msg);
        return XML_STATUS_ERROR;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(msg, sizeof msg, "cannot open external entity '%s': %s",
                 path, strerror(errno));
        free(path);
        set_error(x, ERR_IO, msg);
        return XML_STATUS_ERROR;
    }
    XML_Parser sub = XML_ExternalEntityParserCreate(parser, context, NULL);
    if (!sub) { fclose(f); free(path); oom(x); return XML_STATUS_ERROR; }
    /* The sub-parser resolves its own relative references from its file. */
    char *dir = xstrdup(path);
    if (dir) {
        char *slash = strrchr(dir, '/');
        if (slash) { slash[1] = '\0'; XML_SetBase(sub, dir); }
    }
    XML_Parser saved = x->p;
    x->p = sub;
    x->ext_nesting++;
    int ok = 1;
    char buf[8192];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        int final = n < sizeof buf;
        x->bytes += (int64_t)n;
        if (x->max_bytes > 0 && x->bytes > x->max_bytes) {
            set_error(x, ERR_LIMIT, "input exceeds max-bytes");
            ok = 0;
            break;
        }
        if (XML_Parse(sub, buf, (int)n, final) == XML_STATUS_ERROR) {
            if (!x->failed) {
                snprintf(msg, sizeof msg, "%s (in external entity '%s')",
                         XML_ErrorString(XML_GetErrorCode(sub)), path);
                set_error(x, ERR_SYNTAX, msg);
            }
            ok = 0;
            break;
        }
        if (final) break;
    }
    x->ext_nesting--;
    x->p = saved;
    XML_ParserFree(sub);
    fclose(f);
    free(path);
    free(dir);
    return ok ? XML_STATUS_OK : XML_STATUS_ERROR;
}

/* ---- lifecycle ---------------------------------------------------------- */

void *turxml_new(int ns, int64_t max_depth, int64_t max_bytes,
                 double max_ratio, int allow_ext) {
    turxml *x = (turxml *)calloc(1, sizeof(turxml));
    if (!x) return NULL;
    x->ns = ns;
    x->max_depth = max_depth;
    x->max_bytes = max_bytes;
    x->allow_ext = allow_ext;
    x->p = ns ? XML_ParserCreateNS(NULL, TURXML_NS_SEP) : XML_ParserCreate(NULL);
    if (!x->p) { free(x); return NULL; }
    if (ns) XML_SetReturnNSTriplet(x->p, 1);
    XML_SetUserData(x->p, x);
    XML_SetElementHandler(x->p, on_start, on_end);
    XML_SetCharacterDataHandler(x->p, on_chars);
    XML_SetCommentHandler(x->p, on_comment);
    XML_SetProcessingInstructionHandler(x->p, on_pi);
    XML_SetCdataSectionHandler(x->p, on_start_cdata, on_end_cdata);
    XML_SetStartDoctypeDeclHandler(x->p, on_doctype);
    XML_SetNamespaceDeclHandler(x->p, on_ns_start, NULL);
    XML_SetSkippedEntityHandler(x->p, on_skipped);
    XML_SetExternalEntityRefHandler(x->p, on_external);
    XML_SetParamEntityParsing(x->p, allow_ext
                                       ? XML_PARAM_ENTITY_PARSING_UNLESS_STANDALONE
                                       : XML_PARAM_ENTITY_PARSING_NEVER);
#if XML_GE == 1
    if (max_ratio >= 1.0)
        XML_SetBillionLaughsAttackProtectionMaximumAmplification(x->p,
                                                                 (float)max_ratio);
#else
    (void)max_ratio;
#endif
    return x;
}

void turxml_free(void *xp) {
    turxml *x = (turxml *)xp;
    if (!x) return;
    if (x->p) XML_ParserFree(x->p);
    for (size_t i = 0; i < x->qlen; i++) xev_free(&x->q[x->qhead + i]);
    free(x->q);
    if (x->has_cur) xev_free(&x->cur);
    free(x->text);
    for (int i = 0; i < x->pns; i++) { free(x->pns_prefix[i]); free(x->pns_uri[i]); }
    free(x->pns_prefix);
    free(x->pns_uri);
    if (x->file) fclose(x->file);
    free(x->base);
    free(x->err_msg);
    if (x->doc) turdom_doc_free(x->doc);
    free(x);
}

/* Map expat's error after XML_Parse failed, unless a handler already
 * recorded a more specific one. */
static void record_expat_error(turxml *x) {
    if (x->failed) return;
    enum XML_Error code = XML_GetErrorCode(x->p);
    int kind = ERR_SYNTAX;
#if XML_GE == 1
    if (code == XML_ERROR_AMPLIFICATION_LIMIT_BREACH) kind = ERR_LIMIT;
#endif
    if (code == XML_ERROR_NO_MEMORY) kind = ERR_LIMIT;
    set_error(x, kind, XML_ErrorString(code));
}

/* Feed one chunk.  Returns 0 on success, -1 once the parse has failed. */
int turxml_feed(void *xp, const char *buf, int64_t len, int is_final) {
    turxml *x = (turxml *)xp;
    if (!x || x->failed) return -1;
    if (x->finished || x->stopped) return 0;
    if (len < 0) len = 0;
    x->bytes += len;
    if (x->max_bytes > 0 && x->bytes > x->max_bytes) {
        char msg[96];
        snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                 (long long)x->max_bytes);
        set_error(x, ERR_LIMIT, msg);
        return -1;
    }
    /* XML_Parse takes an int length; feed oversized chunks in pieces. */
    size_t off = 0;
    do {
        size_t n = (size_t)len - off;
        if (n > TURXML_CHUNK) n = TURXML_CHUNK;
        int last = is_final && off + n == (size_t)len;
        if (XML_Parse(x->p, buf ? buf + off : "", (int)n, last) ==
            XML_STATUS_ERROR) {
            record_expat_error(x);
            return -1;
        }
        off += n;
    } while (off < (size_t)len);
    if (x->failed) return -1;
    if (is_final) {
        flush_text(x);
        x->finished = 1;
    }
    return x->failed ? -1 : 0;
}

/* ---- input sources ------------------------------------------------------ */

void turxml_set_string(void *xp, const char *s, int64_t len) {
    turxml *x = (turxml *)xp;
    x->src = s ? s : "";
    x->src_len = len < 0 ? strlen(x->src) : (size_t)len;
    x->src_off = 0;
    x->src_set = 1;
}

/* Open a file as the source.  Returns 0, or -1 with an Io error recorded. */
int turxml_set_file(void *xp, const char *path) {
    turxml *x = (turxml *)xp;
    x->file = fopen(path, "rb");
    if (!x->file) {
        char msg[512];
        snprintf(msg, sizeof msg, "cannot open '%s': %s", path, strerror(errno));
        set_error(x, ERR_IO, msg);
        x->err_line = 0;
        x->err_col = 0;
        return -1;
    }
    /* Relative external entities (when allowed) resolve from the file. */
    free(x->base);
    x->base = xstrdup(path);
    if (x->base) {
        char *slash = strrchr(x->base, '/');
        if (slash) slash[1] = '\0';
        else x->base[0] = '\0';
        XML_SetBase(x->p, x->base);
    }
    x->src_set = 1;
    return 0;
}

/* Feed the next chunk of the configured source.  Returns 1 when more input
 * remains, 0 once the final chunk has been parsed, -1 on failure. */
int turxml_pump(void *xp) {
    turxml *x = (turxml *)xp;
    if (!x || x->failed) return -1;
    if (x->finished || x->stopped || !x->src_set) return 0;
    if (x->file) {
        char *buf = (char *)XML_GetBuffer(x->p, (int)TURXML_CHUNK);
        if (!buf) { record_expat_error(x); return -1; }
        size_t n = fread(buf, 1, TURXML_CHUNK, x->file);
        if (ferror(x->file)) {
            set_error(x, ERR_IO, "read error");
            return -1;
        }
        int final = feof(x->file) != 0 || n == 0;
        x->bytes += (int64_t)n;
        if (x->max_bytes > 0 && x->bytes > x->max_bytes) {
            char msg[96];
            snprintf(msg, sizeof msg, "input exceeds max-bytes (%lld)",
                     (long long)x->max_bytes);
            set_error(x, ERR_LIMIT, msg);
            return -1;
        }
        if (XML_ParseBuffer(x->p, (int)n, final) == XML_STATUS_ERROR) {
            record_expat_error(x);
            return -1;
        }
        if (x->failed) return -1;
        if (final) { flush_text(x); x->finished = 1; return 0; }
        return 1;
    }
    size_t n = x->src_len - x->src_off;
    if (n > TURXML_CHUNK) n = TURXML_CHUNK;
    int final = x->src_off + n == x->src_len;
    int rc = turxml_feed(x, x->src + x->src_off, (int64_t)n, final);
    x->src_off += n;
    if (rc < 0) return -1;
    return final ? 0 : 1;
}

/* ---- draining ----------------------------------------------------------- */

/* Advance to the next queued event.  Returns its kind (> 0), or 0 when the
 * queue is empty (feed more, or the input is done -- check turxml_failed). */
int turxml_next(void *xp) {
    turxml *x = (turxml *)xp;
    if (!x) return 0;
    if (x->has_cur) { xev_free(&x->cur); x->has_cur = 0; }
    if (x->stopped || x->qlen == 0) return 0;
    x->cur = x->q[x->qhead];
    x->qhead++;
    x->qlen--;
    if (x->qlen == 0) x->qhead = 0;
    x->has_cur = 1;
    return x->cur.kind;
}

/* The handler said Stop: discard what is queued and accept no more input. */
void turxml_stop(void *xp) {
    turxml *x = (turxml *)xp;
    if (!x) return;
    x->stopped = 1;
    for (size_t i = 0; i < x->qlen; i++) xev_free(&x->q[x->qhead + i]);
    x->qhead = 0;
    x->qlen = 0;
}

int turxml_failed(void *xp) { return xp ? ((turxml *)xp)->failed : 1; }
int turxml_stopped(void *xp) { return xp ? ((turxml *)xp)->stopped : 0; }
int turxml_err_kind(void *xp) { return xp ? ((turxml *)xp)->err_kind : ERR_SYNTAX; }
const char *turxml_err_msg(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->err_msg) ? x->err_msg : "parse error";
}
int64_t turxml_err_line(void *xp) { return xp ? ((turxml *)xp)->err_line : 0; }
int64_t turxml_err_col(void *xp) { return xp ? ((turxml *)xp)->err_col : 0; }

const char *turxml_ev_name(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur && x->cur.name.sax) ? x->cur.name.sax : "";
}
const char *turxml_ev_text(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur && x->cur.text) ? x->cur.text : "";
}
const char *turxml_ev_pubid(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur) ? x->cur.pubid : NULL;
}
const char *turxml_ev_sysid(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur) ? x->cur.sysid : NULL;
}
int64_t turxml_ev_nattrs(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur) ? x->cur.nattrs : 0;
}
const char *turxml_ev_attr_name(void *xp, int64_t i) {
    turxml *x = (turxml *)xp;
    if (!x || !x->has_cur || i < 0 || i >= x->cur.nattrs) return "";
    return x->cur.attr_names[i].sax;
}
const char *turxml_ev_attr_value(void *xp, int64_t i) {
    turxml *x = (turxml *)xp;
    if (!x || !x->has_cur || i < 0 || i >= x->cur.nattrs) return "";
    return x->cur.attr_values[i];
}
int64_t turxml_ev_line(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur) ? x->cur.line : 0;
}
int64_t turxml_ev_col(void *xp) {
    turxml *x = (turxml *)xp;
    return (x && x->has_cur) ? x->cur.col : 0;
}

/* ---- the tree builder --------------------------------------------------- */

static void build_one(turxml *x, xev *e) {
    turdom_doc *d = x->doc;
    switch (e->kind) {
    case EV_START: {
        turdom_node *el = turdom_append_element(d, x->at, e->name.qname,
                                                e->name.uri, e->name.local,
                                                e->line, e->col);
        if (!el) { oom(x); return; }
        for (int i = 0; i < e->nns; i++) {
            const char *pre = e->ns_prefix[i];
            size_t plen = pre ? strlen(pre) : 0;
            char *q = (char *)malloc(plen + 7);
            if (!q) { oom(x); return; }
            memcpy(q, "xmlns", 5);
            if (pre) { q[5] = ':'; memcpy(q + 6, pre, plen + 1); }
            else q[5] = '\0';
            turdom_add_attr(d, el, q, NULL, NULL, e->ns_uri[i],
                            strlen(e->ns_uri[i]));
            free(q);
        }
        for (int i = 0; i < e->nattrs; i++)
            turdom_add_attr(d, el, e->attr_names[i].qname, e->attr_names[i].uri,
                            e->attr_names[i].local, e->attr_values[i],
                            strlen(e->attr_values[i]));
        x->at = el;
        return;
    }
    case EV_END: {
        turdom_node *p = turdom_parent(x->at);
        if (p) x->at = p;
        return;
    }
    case EV_CHARS:
        if (!turdom_append_chars(d, x->at, x->in_cdata ? TURDOM_CDATA : TURDOM_TEXT,
                                 e->text, e->text_len, e->line, e->col))
            oom(x);
        if (x->in_cdata) x->cdata_has_chars = 1;
        return;
    case EV_COMMENT:
        if (!turdom_append_chars(d, x->at, TURDOM_COMMENT, e->text, e->text_len,
                                 e->line, e->col))
            oom(x);
        return;
    case EV_PI:
        if (!turdom_append_pi(d, x->at, e->name.sax, e->text, e->line, e->col))
            oom(x);
        return;
    case EV_START_CDATA:
        x->in_cdata = 1;
        x->cdata_has_chars = 0;
        return;
    case EV_END_CDATA:
        if (!x->cdata_has_chars &&
            !turdom_append_chars(d, x->at, TURDOM_CDATA, "", 0, e->line, e->col))
            oom(x);
        x->in_cdata = 0;
        return;
    case EV_DOCTYPE:
        if (!turdom_append_doctype(d, x->at, e->name.sax, e->pubid, e->sysid,
                                   e->line, e->col))
            oom(x);
        return;
    default:
        return;
    }
}

static void build_drain(turxml *x) {
    while (x->qlen > 0) {
        xev *e = &x->q[x->qhead];
        if (!x->failed) build_one(x, e);
        xev_free(e);
        x->qhead++;
        x->qlen--;
    }
    x->qhead = 0;
}

/* Parse the configured source into a tree.  Returns the turdom_doc (now
 * owned by the caller), or NULL with the error recorded on x. */
void *turxml_dom_run(void *xp) {
    turxml *x = (turxml *)xp;
    if (!x) return NULL;
    x->doc = turdom_doc_new(TURDOM_FLAVOR_XML);
    if (!x->doc) { set_error(x, ERR_LIMIT, "out of memory"); return NULL; }
    x->at = turdom_doc_node(x->doc);
    for (;;) {
        int rc = turxml_pump(x);
        build_drain(x);
        if (rc < 0 || x->failed) return NULL;
        if (rc == 0) break;
    }
    turdom_doc *d = x->doc;
    x->doc = NULL;
    return d;
}
