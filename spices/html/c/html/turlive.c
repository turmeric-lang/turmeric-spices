/* c/html/turlive.c -- live `#html-live{...}` templates: a tree built from a
 * literal whose holes are remembered, so `tpl-update!` rewrites only the
 * holes whose values changed (xml-html-followups-plan follow-up A4).
 *
 * A `turlive_html` is one evaluation of a live literal: its template (the
 * string dom/markupc produced, see dom-core's c/dom/turmarkup.c) and a
 * snapshot of every hole's value, already rendered -- a content hole as its
 * markup, an attribute hole as its raw value and state (0 absent, 1 present
 * as a boolean, 2 a value).  The snapshot owns copies, so the caller's
 * strings may go away.
 *
 * Mounting parses the template with each hole marked (as the first-use check
 * does), then:
 *   - replaces each content hole's marker comment with an empty text node,
 *     the hole's ANCHOR, and inserts the hole's nodes after it (its markup
 *     parsed in the hole's parent element);
 *   - groups the attribute holes by (element, attribute): an attribute's
 *     value is its template text with each marker replaced by its hole's
 *     value, and a whole-value hole sets, empties or removes the attribute.
 * An update compares each hole's new snapshot with the old one and patches
 * only what differs: a changed content hole's nodes are removed and its new
 * ones inserted after the anchor; a changed attribute group is set again.
 * Each patch counts once, which is what the tests pin.
 *
 * The declarations below repeat dom-core's and tur-html's own: the
 * :c-includes of one spice are not exported to another (see turhtml.c). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct turdom_doc turdom_doc;
typedef struct turdom_node turdom_node;
typedef struct turdom_mbuf turdom_mbuf;

extern void         turdom_doc_free(turdom_doc *);
extern turdom_node *turdom_doc_node(turdom_doc *);
extern turdom_doc  *turdom_node_doc(const turdom_node *);
extern turdom_node *turdom_next_in(const turdom_node *, const turdom_node *);
extern turdom_node *turdom_parent(const turdom_node *);
extern turdom_node *turdom_first_child(const turdom_node *);
extern turdom_node *turdom_next_sibling(const turdom_node *);
extern int          turdom_kind(const turdom_node *);
extern const char  *turdom_value(const turdom_node *);
extern const char  *turdom_name(const turdom_node *);
extern const char  *turdom_local_name(const turdom_node *);
extern int64_t      turdom_attr_count(const turdom_node *);
extern const char  *turdom_attr_name_at(const turdom_node *, int64_t);
extern const char  *turdom_attr_value_at(const turdom_node *, int64_t);
extern turdom_node *turdom_create_chars(turdom_doc *, int, const char *, size_t);
extern int          turdom_replace(turdom_node *, turdom_node *);
extern turdom_node *turdom_import(turdom_doc *, const turdom_node *);
extern int          turdom_insert_after(turdom_node *, turdom_node *);
extern void         turdom_remove(turdom_node *);
extern int          turdom_edit_set_attr(turdom_node *, const char *, const char *,
                                         const char *, size_t);
extern int          turdom_remove_attr(turdom_node *, const char *);
extern int          turdom_set_value(turdom_node *, const char *, size_t);
extern int          turdom_check_subtree(const turdom_doc *, const turdom_node *);

extern char        *turdom_tpl_check_markup(const char *);
extern int64_t      turdom_tpl_hole_count(const char *);
extern int          turdom_tpl_hole_kind(const char *, int64_t);
extern int64_t      turdom_tpl_next_hole_ref(const char *, int64_t *);
extern int          turdom_tpl_is_hole_ref(const char *, int64_t);
extern int64_t      turdom_tpl_static(turdom_mbuf *, const char *, int64_t);
extern int64_t      turdom_tpl_after(const char *, int64_t);
extern void         turdom_tpl_whole_attr(turdom_mbuf *, const char *, int64_t, int,
                                          const char *, int);
extern turdom_mbuf *turdom_mbuf_new(void);
extern void         turdom_mbuf_put(turdom_mbuf *, const char *);
extern void         turdom_mbuf_put_escaped(turdom_mbuf *, const char *, int, int);
extern char        *turdom_mbuf_finish(turdom_mbuf *);

extern void *turhtml_parse(const char *, int64_t, int64_t, int64_t);
extern void *turhtml_parse_fragment(const char *, const char *, int64_t, int64_t, int64_t);

#define TEXT_KIND    2
#define COMMENT_KIND 3
#define ELEMENT_KIND 1

static char *xdup(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *d = (char *)malloc(n + 1);
    if (!d) abort();
    if (n) memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

/* Can every top-level node of a parsed raw-html value be written back and
 * parse the same way?  dom/edit's checks (no <plaintext>, no comment or raw
 * text that would end early).  Here rather than in html/markup's inline C
 * because a declaration there would clash with dom-core's own declarations
 * of the same turdom functions when both land in one translation unit. */
int turlive_raw_writable(turdom_doc *d) {
    for (turdom_node *c = turdom_first_child(turdom_doc_node(d)); c; c = turdom_next_sibling(c))
        if (turdom_check_subtree(d, c) != 0) return 0;
    return 1;
}

/* ---- one evaluation of a live literal ---------------------------------------- */

typedef struct turlive_html {
    const char *tpl;
    int64_t n;
    int *state;
    char **str;
} turlive_html;

turlive_html *turlive_html_new(const char *tpl) {
    turlive_html *h = (turlive_html *)calloc(1, sizeof *h);
    if (!h) abort();
    h->tpl = tpl;
    h->n = turdom_tpl_hole_count(tpl);
    h->state = (int *)calloc((size_t)(h->n ? h->n : 1), sizeof(int));
    h->str = (char **)calloc((size_t)(h->n ? h->n : 1), sizeof(char *));
    if (!h->state || !h->str) abort();
    return h;
}

void turlive_html_set(turlive_html *h, int64_t i, int64_t state, const char *s) {
    if (!h || i < 0 || i >= h->n) return;
    free(h->str[i]);
    h->state[i] = (int)state;
    h->str[i] = xdup(s);
}

void turlive_html_free(turlive_html *h) {
    if (!h) return;
    for (int64_t i = 0; i < h->n; i++) free(h->str[i]);
    free(h->state);
    free(h->str);
    free(h);
}

/* The markup, as html->string would write it. */
char *turlive_html_string(const turlive_html *h) {
    turdom_mbuf *b = turdom_mbuf_new();
    int64_t pos = 0, i = 0;
    for (;;) {
        pos = turdom_tpl_static(b, h->tpl, pos);
        if (pos < 0) break;
        int kind = turdom_tpl_hole_kind(h->tpl, i);
        if (kind == 'c') {
            turdom_mbuf_put(b, h->str[i]);
        } else if (kind == 'a' || kind == 'k') {
            turdom_mbuf_put_escaped(b, h->str[i], 1, 1);
        } else if (kind == 'r') {
            /* a code hole in a <script>/<style> body: escaped for its
             * language, and checked, when the snapshot was taken */
            turdom_mbuf_put(b, h->str[i]);
        } else {
            turdom_tpl_whole_attr(b, h->tpl, pos, h->state[i], h->str[i], 1);
        }
        i++;
        pos = turdom_tpl_after(h->tpl, pos);
    }
    return turdom_mbuf_finish(b);
}

/* ---- the mounted tree ------------------------------------------------------------ */

typedef struct group {
    turdom_node *el;
    char *name;     /* the attribute's name as parsed */
    char *tpl;      /* its value with __tur_hole_N__ markers */
    int whole;      /* one whole-value hole */
    int text;       /* el is the text of a <script>/<style> with code holes */
    int dirty;
} group;

typedef struct turlive_inst {
    const char *tpl;
    turdom_doc *doc;
    int64_t n;
    int *state;
    char **str;
    turdom_node **anchor;     /* content holes */
    char **ctx;               /* their parent element ("template" at the top) */
    turdom_node ***nodes;
    int64_t *nnodes;
    int64_t *grp;             /* attribute holes: their group */
    group *groups;
    int64_t ngroups;
    int64_t touched;          /* patches made by the last update */
} turlive_inst;

/* Parse markup as the content of ctx and insert its nodes after anchor. */
static void insert_markup(turlive_inst *t, int64_t i) {
    free(t->nodes[i]);
    t->nodes[i] = NULL;
    t->nnodes[i] = 0;
    const char *m = t->str[i] ? t->str[i] : "";
    if (!m[0]) return;
    turdom_doc *frag = (turdom_doc *)turhtml_parse_fragment(t->ctx[i], m, -1, 512, 0);
    if (!frag) return;
    int64_t cap = 0;
    turdom_node *prev = t->anchor[i];
    for (turdom_node *c = turdom_first_child(turdom_doc_node(frag)); c; c = turdom_next_sibling(c)) {
        turdom_node *imp = turdom_import(t->doc, c);
        if (!imp) continue;
        if (turdom_insert_after(prev, imp) != 0) continue;
        if (t->nnodes[i] == cap) {
            cap = cap ? cap * 2 : 4;
            t->nodes[i] = (turdom_node **)realloc(t->nodes[i], (size_t)cap * sizeof(turdom_node *));
            if (!t->nodes[i]) abort();
        }
        t->nodes[i][t->nnodes[i]++] = imp;
        prev = imp;
    }
    turdom_doc_free(frag);
}

static void remove_markup(turlive_inst *t, int64_t i) {
    for (int64_t k = 0; k < t->nnodes[i]; k++) turdom_remove(t->nodes[i][k]);
    t->nnodes[i] = 0;
}

/* Recompute and set one attribute group from the holes' current values. */
static void apply_group(turlive_inst *t, int64_t g) {
    group *gr = &t->groups[g];
    if (gr->whole) {
        int64_t at = 0, k = turdom_tpl_next_hole_ref(gr->tpl, &at);
        if (k < 0) return;
        if (t->state[k] == 0) turdom_remove_attr(gr->el, gr->name);
        else turdom_edit_set_attr(gr->el, gr->name, NULL, t->state[k] == 1 ? "" : t->str[k],
                                  t->state[k] == 1 ? 0 : strlen(t->str[k]));
        return;
    }
    /* The template text with each marker replaced by its hole's value. */
    size_t cap = strlen(gr->tpl) + 64, n = 0;
    char *v = (char *)malloc(cap);
    if (!v) abort();
    int64_t at = 0;
    for (;;) {
        int64_t before = at;
        int64_t k = turdom_tpl_next_hole_ref(gr->tpl, &at);
        size_t mlen = 0;
        const char *ins = "";
        if (k >= 0) {
            char mk[64];
            mlen = (size_t)snprintf(mk, sizeof mk, "__tur_hole_%lld__", (long long)k);
            ins = t->state[k] == 1 ? "true" : t->state[k] == 0 ? "" : t->str[k];
        }
        /* the static text up to this marker (or to the end) */
        size_t sl = (size_t)(at - before) - mlen;
        size_t il = strlen(ins);
        while (n + sl + il + 1 > cap) {
            cap *= 2;
            v = (char *)realloc(v, cap);
            if (!v) abort();
        }
        memcpy(v + n, gr->tpl + before, sl);
        n += sl;
        memcpy(v + n, ins, il);
        n += il;
        if (k < 0) break;
    }
    v[n] = '\0';
    if (gr->text) turdom_set_value(gr->el, v, n);
    else turdom_edit_set_attr(gr->el, gr->name, NULL, v, n);
    free(v);
}

static void take_values(turlive_inst *t, const turlive_html *h) {
    for (int64_t i = 0; i < t->n; i++) {
        free(t->str[i]);
        t->str[i] = xdup(h->str[i]);
        t->state[i] = h->state[i];
    }
}

/* Build the tree.  NULL only when the template does not parse the way the
 * first-use check saw it (it has already been checked, so: never). */
turlive_inst *turlive_mount(const turlive_html *h) {
    turlive_inst *t = (turlive_inst *)calloc(1, sizeof *t);
    if (!t) abort();
    t->tpl = h->tpl;
    t->n = h->n;
    size_t n1 = (size_t)(t->n ? t->n : 1);
    t->state = (int *)calloc(n1, sizeof(int));
    t->str = (char **)calloc(n1, sizeof(char *));
    t->anchor = (turdom_node **)calloc(n1, sizeof(turdom_node *));
    t->ctx = (char **)calloc(n1, sizeof(char *));
    t->nodes = (turdom_node ***)calloc(n1, sizeof(turdom_node **));
    t->nnodes = (int64_t *)calloc(n1, sizeof(int64_t));
    t->grp = (int64_t *)calloc(n1, sizeof(int64_t));
    t->groups = (group *)calloc(n1, sizeof(group));
    if (!t->state || !t->str || !t->anchor || !t->ctx || !t->nodes || !t->nnodes ||
        !t->grp || !t->groups)
        abort();
    take_values(t, h);

    char *cm = turdom_tpl_check_markup(t->tpl);
    t->doc = (turdom_doc *)(t->tpl[0] == 'D'
                                ? turhtml_parse(cm, -1, 512, 0)
                                : turhtml_parse_fragment("template", cm, -1, 512, 0));
    free(cm);
    if (!t->doc) abort();

    /* Collect the marker comments first: replacing them mid-walk would
     * disturb the walk. */
    turdom_node *top = turdom_doc_node(t->doc);
    turdom_node **marks = (turdom_node **)calloc(n1, sizeof(turdom_node *));
    if (!marks) abort();
    for (turdom_node *x = turdom_next_in(top, NULL); x; x = turdom_next_in(top, x)) {
        int kind = turdom_kind(x);
        if (kind == COMMENT_KIND) {
            const char *v = turdom_value(x);
            int64_t at = 0, k = turdom_tpl_next_hole_ref(v, &at);
            if (k >= 0 && k < t->n && turdom_tpl_is_hole_ref(v, k)) {
                marks[k] = x;
                turdom_node *p = turdom_parent(x);
                const char *pn = (p && turdom_kind(p) == ELEMENT_KIND)
                                     ? (turdom_local_name(p) ? turdom_local_name(p) : turdom_name(p))
                                     : "template";
                t->ctx[k] = xdup(pn);
            }
        } else if (kind == TEXT_KIND) {
            /* code holes in a <script> or <style> body (plan A6): the text
             * is one group, rebuilt from its holes like an attribute's value */
            const char *v = turdom_value(x);
            if (!v || !strstr(v, "__tur_hole_")) continue;
            int64_t g = t->ngroups++;
            group *gr = &t->groups[g];
            gr->el = x;
            gr->text = 1;
            gr->tpl = xdup(v);
            int64_t at = 0, k;
            while ((k = turdom_tpl_next_hole_ref(v, &at)) >= 0)
                if (k < t->n) t->grp[k] = g;
        } else if (kind == ELEMENT_KIND) {
            int64_t na = turdom_attr_count(x);
            for (int64_t a = 0; a < na; a++) {
                const char *av = turdom_attr_value_at(x, a);
                if (!av || !strstr(av, "__tur_hole_")) continue;
                int64_t g = t->ngroups++;
                group *gr = &t->groups[g];
                gr->el = x;
                gr->name = xdup(turdom_attr_name_at(x, a));
                gr->tpl = xdup(av);
                int64_t at = 0, k;
                while ((k = turdom_tpl_next_hole_ref(av, &at)) >= 0)
                    if (k < t->n) {
                        t->grp[k] = g;
                        if (turdom_tpl_hole_kind(t->tpl, k) == 'w') gr->whole = 1;
                    }
            }
        }
    }
    for (int64_t k = 0; k < t->n; k++) {
        if (!marks[k]) continue;
        turdom_node *anchor = turdom_create_chars(t->doc, TEXT_KIND, "", 0);
        if (!anchor || turdom_replace(marks[k], anchor) != 0) abort();
        t->anchor[k] = anchor;
        insert_markup(t, k);
    }
    free(marks);
    for (int64_t g = 0; g < t->ngroups; g++) apply_group(t, g);
    return t;
}

/* Patch the tree to h's values.  Returns the number of patches made, or -1
 * when h is an evaluation of a different literal. */
int64_t turlive_update(turlive_inst *t, const turlive_html *h) {
    if (!t || !h || h->tpl != t->tpl || h->n != t->n) return -1;
    int64_t touched = 0;
    for (int64_t g = 0; g < t->ngroups; g++) t->groups[g].dirty = 0;
    for (int64_t i = 0; i < t->n; i++) {
        int same = t->state[i] == h->state[i] &&
                   strcmp(t->str[i] ? t->str[i] : "", h->str[i] ? h->str[i] : "") == 0;
        if (same) continue;
        free(t->str[i]);
        t->str[i] = xdup(h->str[i]);
        t->state[i] = h->state[i];
        if (t->anchor[i]) {
            remove_markup(t, i);
            insert_markup(t, i);
            touched++;
        } else if (t->ngroups) {
            t->groups[t->grp[i]].dirty = 1;
        }
    }
    for (int64_t g = 0; g < t->ngroups; g++)
        if (t->groups[g].dirty) {
            apply_group(t, g);
            touched++;
        }
    t->touched = touched;
    return touched;
}

void *turlive_doc(const turlive_inst *t) { return t ? t->doc : NULL; }

void turlive_free(turlive_inst *t) {
    if (!t) return;
    for (int64_t i = 0; i < t->n; i++) {
        free(t->str[i]);
        free(t->ctx[i]);
        free(t->nodes[i]);
    }
    for (int64_t g = 0; g < t->ngroups; g++) {
        free(t->groups[g].name);
        free(t->groups[g].tpl);
    }
    free(t->state);
    free(t->str);
    free(t->anchor);
    free(t->ctx);
    free(t->nodes);
    free(t->nnodes);
    free(t->grp);
    free(t->groups);
    turdom_doc_free(t->doc);
    free(t);
}
