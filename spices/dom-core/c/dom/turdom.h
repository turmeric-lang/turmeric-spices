/* c/dom/turdom.h -- the shared markup tree behind tur-dom-core.
 *
 * One arena-owned tree type that both tur-xml (expat) and tur-html (lexbor)
 * build into, so the navigation, query, text and serialization code exists
 * once.  A turdom_doc owns every node and every string under it; freeing the
 * doc frees them all in one pass over the arena's chunks.
 *
 * Nothing here recurses on tree depth: every walk is iterative over the
 * parent/sibling links, so a hostile document that slipped past a depth cap
 * still cannot exhaust the C stack in a reader.
 *
 * Consumers in other spices see only the .tur exports, so the builder entry
 * points are re-declared `extern` in c/xml/turxml.c and c/html/turhtml.c (the
 * :c-includes of one spice are not exported to another).  Keep the three in
 * step.
 */
#ifndef TURDOM_H
#define TURDOM_H

#include <stddef.h>
#include <stdint.h>

enum {
    TURDOM_DOCUMENT = 0,
    TURDOM_ELEMENT  = 1,
    TURDOM_TEXT     = 2,
    TURDOM_COMMENT  = 3,
    TURDOM_CDATA    = 4,
    TURDOM_PI       = 5,
    TURDOM_DOCTYPE  = 6
};

enum {
    TURDOM_FLAVOR_XML  = 0,
    TURDOM_FLAVOR_HTML = 1
};

typedef struct turdom_doc  turdom_doc;
typedef struct turdom_node turdom_node;

/* ---- building ---------------------------------------------------------- */

turdom_doc  *turdom_doc_new(int flavor);
void         turdom_doc_free(turdom_doc *d);
int          turdom_doc_flavor(const turdom_doc *d);
turdom_node *turdom_doc_node(turdom_doc *d);
turdom_doc  *turdom_node_doc(const turdom_node *n);

/* qname is the name as written (`atom:feed`); ns_uri and local may be NULL
 * when the parser did no namespace processing.  line/col are 1-based, 0 when
 * the parser does not track positions. */
turdom_node *turdom_append_element(turdom_doc *d, turdom_node *parent,
                                   const char *qname, const char *ns_uri,
                                   const char *local,
                                   int64_t line, int64_t col);
void         turdom_add_attr(turdom_doc *d, turdom_node *el,
                             const char *qname, const char *ns_uri,
                             const char *local,
                             const char *value, size_t vlen);
/* kind is TURDOM_TEXT, TURDOM_COMMENT or TURDOM_CDATA. */
turdom_node *turdom_append_chars(turdom_doc *d, turdom_node *parent, int kind,
                                 const char *s, size_t n,
                                 int64_t line, int64_t col);
turdom_node *turdom_append_pi(turdom_doc *d, turdom_node *parent,
                              const char *target, const char *data,
                              int64_t line, int64_t col);
turdom_node *turdom_append_doctype(turdom_doc *d, turdom_node *parent,
                                   const char *name, const char *public_id,
                                   const char *system_id,
                                   int64_t line, int64_t col);

/* ---- reading ----------------------------------------------------------- */

int          turdom_kind(const turdom_node *n);
const char  *turdom_name(const turdom_node *n);
const char  *turdom_local_name(const turdom_node *n);
const char  *turdom_ns_uri(const turdom_node *n);
const char  *turdom_value(const turdom_node *n);
int64_t      turdom_value_len(const turdom_node *n);
const char  *turdom_public_id(const turdom_node *n);
const char  *turdom_system_id(const turdom_node *n);
int64_t      turdom_line(const turdom_node *n);
int64_t      turdom_col(const turdom_node *n);

turdom_node *turdom_parent(const turdom_node *n);
turdom_node *turdom_first_child(const turdom_node *n);
turdom_node *turdom_last_child(const turdom_node *n);
turdom_node *turdom_next_sibling(const turdom_node *n);
turdom_node *turdom_prev_sibling(const turdom_node *n);
int64_t      turdom_child_count(const turdom_node *n);
turdom_node *turdom_root_element(turdom_doc *d);

int64_t      turdom_attr_count(const turdom_node *n);
const char  *turdom_attr_name_at(const turdom_node *n, int64_t i);
const char  *turdom_attr_value_at(const turdom_node *n, int64_t i);
const char  *turdom_attr_ns_uri_at(const turdom_node *n, int64_t i);
const char  *turdom_attr_local_at(const turdom_node *n, int64_t i);
const char  *turdom_attr_get(const turdom_node *n, const char *name);

/* Does element n answer to `name`?  Matches the qualified name as written,
 * the Clark form `{uri}local`, and -- for HTML documents -- ignores ASCII
 * case.  Non-elements never match. */
int          turdom_element_matches(const turdom_node *n, const char *name);

/* Document-order walks restricted to root's subtree (root itself excluded).
 * from == NULL starts the walk.  Returns NULL at the end. */
turdom_node *turdom_next_in(const turdom_node *root, const turdom_node *from);
turdom_node *turdom_prev_in(const turdom_node *root, const turdom_node *from);
/* Same, but only elements matching `name` (turdom_element_matches). */
turdom_node *turdom_find_next(const turdom_node *root, const turdom_node *from,
                              const char *name);
turdom_node *turdom_find_prev(const turdom_node *root, const turdom_node *from,
                              const char *name);

/* malloc'd, NUL-terminated; *len gets the byte length.  Caller frees. */
char        *turdom_text_content(const turdom_node *n, size_t *len);
char        *turdom_serialize(const turdom_node *n, size_t *len);

int          turdom_equal(const turdom_node *a, const turdom_node *b);
int64_t      turdom_depth(const turdom_node *n);
int64_t      turdom_node_count(const turdom_doc *d);
/* Arena bytes held by all live documents in the process. */
int64_t      turdom_live_bytes(void);

#endif /* TURDOM_H */
