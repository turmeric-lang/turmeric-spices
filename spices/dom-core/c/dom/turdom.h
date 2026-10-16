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
/* Nodes the document's arena holds, attached or detached (the document node
 * itself not counted). */
int64_t      turdom_node_count(const turdom_doc *d);
/* Arena bytes held by all live documents in the process. */
int64_t      turdom_live_bytes(void);

/* ---- editing ------------------------------------------------------------
 *
 * Nothing is freed before turdom_doc_free.  A replaced string, a removed
 * attribute or a removed subtree stays in the arena, so every pointer a
 * reader borrowed stays valid: a removed node is DETACHED (parent NULL), not
 * dangling, and can be read, re-inserted or ignored.  turdom_garbage_bytes
 * counts what edits abandoned for certain (replaced strings and removed
 * attributes; not detached nodes, which a caller may still hold), and
 * turdom_compact copies the reachable tree into a fresh document.
 *
 * Every function here is iterative.  None validates names or content: that
 * is the caller's job (dom/edit), because the serializer writes names,
 * comment bodies, PI data and HTML raw text verbatim. */

enum {
    TURDOM_OK              = 0,
    TURDOM_E_WRONG_DOC     = 1,  /* the node belongs to another arena: import it */
    TURDOM_E_CYCLE         = 2,  /* inserting an ancestor under its descendant */
    TURDOM_E_NOT_CONTAINER = 3,  /* children under a text/comment/CDATA/PI/doctype */
    TURDOM_E_HIERARCHY     = 4,  /* a document node, a second root element or doctype,
                                    text at the top level, a doctype in an element,
                                    or a doctype after the root element */
    TURDOM_E_DETACHED      = 5,  /* the reference node has no parent */
    TURDOM_E_BAD_KIND      = 6,  /* the operation does not apply to this kind */
    TURDOM_E_OOM           = 7
};

/* Detached nodes, owned by d's arena until inserted.  In an HTML document,
 * an element created with no namespace or the XHTML namespace has its name
 * lower-cased, as createElement does.  kind is TURDOM_TEXT, TURDOM_COMMENT or
 * TURDOM_CDATA.  NULL only on allocation failure. */
turdom_node *turdom_create_element(turdom_doc *d, const char *qname,
                                   const char *ns_uri, const char *local);
turdom_node *turdom_create_chars(turdom_doc *d, int kind, const char *s,
                                 size_t n);
turdom_node *turdom_create_pi(turdom_doc *d, const char *target,
                              const char *data);

/* Tree surgery.  A child that is attached elsewhere is moved.  O(1) apart
 * from the cycle check, which is O(depth). */
int          turdom_append(turdom_node *parent, turdom_node *child);
int          turdom_prepend(turdom_node *parent, turdom_node *child);
int          turdom_insert_before(turdom_node *ref, turdom_node *child);
int          turdom_insert_after(turdom_node *ref, turdom_node *child);
int          turdom_replace(turdom_node *old, turdom_node *repl);
/* Detach n; a no-op when it is already detached. */
void         turdom_remove(turdom_node *n);
/* n's children take its place, then n is detached. */
int          turdom_unwrap(turdom_node *n);

/* Content.  set_attr replaces the value of a matching attribute in place
 * (matched by namespace and local name when ns_uri is given, otherwise by
 * qualified name, ignoring ASCII case in an HTML document) or appends a new
 * one.  remove_attr matches as turdom_attr_get does and returns 1 when it
 * removed something.  set_value sets the data of a text, comment, CDATA or
 * PI node.  set_text is textContent=: on an element or the document node it
 * detaches every child and appends one text node (none for ""). */
int          turdom_set_attr(turdom_node *el, const char *qname,
                             const char *ns_uri, const char *local,
                             const char *value, size_t vlen);
int          turdom_remove_attr(turdom_node *el, const char *name);
int          turdom_set_value(turdom_node *n, const char *s, size_t len);
int          turdom_set_text(turdom_node *n, const char *s, size_t len);
int          turdom_rename(turdom_node *el, const char *qname,
                           const char *ns_uri, const char *local);

/* Deep copies, detached.  clone copies into n's own arena; import copies into
 * d (another document, or the same one).  The document node itself cannot be
 * copied: NULL. */
turdom_node *turdom_clone(const turdom_node *n);
turdom_node *turdom_import(turdom_doc *d, const turdom_node *n);

/* Merge adjacent text nodes and drop empty ones in n's subtree, as the DOM's
 * normalize() does.  A parser only ever produces normalized trees. */
void         turdom_normalize(turdom_node *n);

/* A fresh document holding a copy of d's reachable tree.  The one operation
 * that invalidates handles: nodes of d do not belong to the copy.  The caller
 * frees both. */
turdom_doc  *turdom_compact(const turdom_doc *d);
int64_t      turdom_garbage_bytes(const turdom_doc *d);
/* Is n reachable from its document node? */
int          turdom_attached(const turdom_node *n);

#endif /* TURDOM_H */
