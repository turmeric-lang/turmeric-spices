/* fixtures/c/writer_test.c -- the C-level unit test for turdom's streaming
 * writer (xml-html-parsers-plan follow-up B4).  fixtures/run.sh builds it with
 * -fsanitize=address,undefined against c/dom/turdom.c and runs it.
 *
 * Each case drives a writer through a short event script and checks the
 * output byte for byte, or checks which call refused and that the refusal
 * stuck. */

#include "dom/turdom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_checks = 0, g_fails = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { g_fails++; printf("not ok - %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

static const char *XMLNS_XML = "http://www.w3.org/XML/1998/namespace";

static turdom_writer *X(void) { return turdom_writer_new(TURDOM_FLAVOR_XML); }
static turdom_writer *H(void) { return turdom_writer_new(TURDOM_FLAVOR_HTML); }

static int S(turdom_writer *w, const char *name) { return turdom_writer_start(w, name); }
static int E(turdom_writer *w, const char *name) { return turdom_writer_end(w, name); }
static int A(turdom_writer *w, const char *name, const char *v) {
    return turdom_writer_attr(w, name, v, strlen(v));
}
static int T(turdom_writer *w, const char *s) { return turdom_writer_chars(w, s, strlen(s)); }
static int C(turdom_writer *w, const char *s) { return turdom_writer_comment(w, s, strlen(s)); }

/* Finish w, compare with want, free w. */
static int out_is(turdom_writer *w, const char *want) {
    size_t len = 0;
    int rc = -1;
    char *s = turdom_writer_finish(w, &len, &rc);
    int ok = s && rc == TURDOM_OK && strlen(want) == len && memcmp(s, want, len) == 0;
    if (!ok) printf("#   got:  %s (rc %d)\n#   want: %s\n", s ? s : "(null)", rc, want);
    free(s);
    turdom_writer_free(w);
    return ok;
}

/* Finish w expecting refusal want, free w. */
static int refused(turdom_writer *w, int want) {
    size_t len = 7;
    int rc = -1;
    char *s = turdom_writer_finish(w, &len, &rc);
    int ok = !s && rc == want && len == 0 && turdom_writer_error(w) == want;
    if (!ok) printf("#   finish: %s rc %d, want refusal %d\n", s ? s : "(null)", rc, want);
    free(s);
    turdom_writer_free(w);
    return ok;
}

static void test_xml_basic(void) {
    turdom_writer *w = X();
    CHECK(S(w, "a") == TURDOM_OK);
    CHECK(A(w, "x", "1&\"<>\t\n\r") == TURDOM_OK);
    CHECK(T(w, "t<&>\"") == TURDOM_OK);
    CHECK(S(w, "b") == TURDOM_OK);
    CHECK(E(w, "b") == TURDOM_OK);
    CHECK(C(w, "c") == TURDOM_OK);
    CHECK(turdom_writer_pi(w, "p", "d") == TURDOM_OK);
    CHECK(turdom_writer_pi(w, "q", NULL) == TURDOM_OK);
    CHECK(turdom_writer_cdata(w, 1) == TURDOM_OK);
    CHECK(T(w, "x]]") == TURDOM_OK);
    CHECK(T(w, "<y") == TURDOM_OK);
    CHECK(turdom_writer_cdata(w, 0) == TURDOM_OK);
    CHECK(turdom_writer_cdata(w, 1) == TURDOM_OK);
    CHECK(turdom_writer_cdata(w, 0) == TURDOM_OK);
    CHECK(E(w, "a") == TURDOM_OK);
    CHECK(out_is(w, "<a x=\"1&amp;&quot;&lt;&gt;&#9;&#10;&#13;\">t&lt;&amp;&gt;\"<b/>"
                    "<!--c--><?p d?><?q?><![CDATA[x]]<y]]><![CDATA[]]></a>"));

    /* The prolog and epilog. */
    w = X();
    CHECK(C(w, " head ") == TURDOM_OK);
    CHECK(turdom_writer_doctype(w, "a", "-//P//EN", NULL) == TURDOM_OK);
    CHECK(S(w, "a") == TURDOM_OK);
    CHECK(E(w, "a") == TURDOM_OK);
    CHECK(turdom_writer_pi(w, "tail", "x") == TURDOM_OK);
    CHECK(C(w, "end") == TURDOM_OK);
    CHECK(out_is(w, "<!-- head --><!DOCTYPE a PUBLIC \"-//P//EN\" \"\"><a/><?tail x?><!--end-->"));

    w = X();
    CHECK(turdom_writer_doctype(w, "a", "", "sys\"x") == TURDOM_OK);
    CHECK(S(w, "a") == TURDOM_OK && E(w, "a") == TURDOM_OK);
    CHECK(out_is(w, "<!DOCTYPE a SYSTEM 'sys\"x'><a/>"));

    /* Namespaces off: any Name, colons and all, as written. */
    w = X();
    CHECK(S(w, "a:b:c") == TURDOM_OK);
    CHECK(A(w, "x:y:z", "1") == TURDOM_OK);
    CHECK(A(w, ":w", "2") == TURDOM_OK);
    CHECK(E(w, "a:b:c") == TURDOM_OK);
    CHECK(out_is(w, "<a:b:c x:y:z=\"1\" :w=\"2\"/>"));

    /* Empty chars are nothing, even outside the root. */
    w = X();
    CHECK(T(w, "") == TURDOM_OK);
    CHECK(turdom_writer_chars(w, NULL, 0) == TURDOM_OK);
    CHECK(S(w, "a") == TURDOM_OK && E(w, "a") == TURDOM_OK);
    CHECK(out_is(w, "<a/>"));
}

static void test_xml_clark(void) {
    /* An element gets a default declaration; a plain name under that
     * generated default is put back in no namespace. */
    turdom_writer *w = X();
    S(w, "{urn:u}a");
    S(w, "{urn:u}b"); E(w, "{urn:u}b");
    S(w, "c"); S(w, "d"); E(w, "d"); E(w, "c");
    S(w, "{}e"); E(w, "{}e");
    E(w, "{urn:u}a");
    CHECK(out_is(w, "<a xmlns=\"urn:u\"><b/><c xmlns=\"\"><d/></c><e xmlns=\"\"/></a>"));

    /* An attribute gets a generated prefix, reused by its siblings and
     * descendants; a namespace change below re-declares. */
    w = X();
    S(w, "{urn:u}a");
    A(w, "{urn:v}x", "1");
    A(w, "{urn:v}y", "2");
    A(w, "plain", "3");
    S(w, "{urn:v}b"); E(w, "{urn:v}b");
    S(w, "{urn:w}c"); A(w, "{urn:v}z", "4"); E(w, "{urn:w}c");
    E(w, "{urn:u}a");
    CHECK(out_is(w, "<a xmlns=\"urn:u\" xmlns:ns1=\"urn:v\" ns1:x=\"1\" ns1:y=\"2\" plain=\"3\">"
                    "<ns1:b/><c xmlns=\"urn:w\" ns1:z=\"4\"/></a>"));

    /* An explicit declaration on the element steers its own name. */
    w = X();
    S(w, "{urn:u}a");
    A(w, "xmlns:p", "urn:u");
    S(w, "{urn:u}b"); A(w, "{urn:u}k", "v"); E(w, "{urn:u}b");
    E(w, "{urn:u}a");
    CHECK(out_is(w, "<p:a xmlns:p=\"urn:u\"><p:b p:k=\"v\"/></p:a>"));

    /* ... and an explicit default that says otherwise forces a prefix. */
    w = X();
    S(w, "{urn:u}a");
    A(w, "xmlns", "urn:v");
    S(w, "{urn:v}b"); E(w, "{urn:v}b");
    E(w, "{urn:u}a");
    CHECK(out_is(w, "<ns1:a xmlns:ns1=\"urn:u\" xmlns=\"urn:v\"><b/></ns1:a>"));

    /* A generated prefix skips one declared on the same element. */
    w = X();
    S(w, "r");
    A(w, "xmlns:ns1", "urn:taken");
    A(w, "{urn:v}x", "1");
    E(w, "r");
    CHECK(out_is(w, "<r xmlns:ns1=\"urn:taken\" xmlns:ns2=\"urn:v\" ns2:x=\"1\"/>"));

    /* {}name means no namespace even under an explicit default; a plain
     * name is left as written there (namespaces off). */
    w = X();
    S(w, "r"); A(w, "xmlns", "urn:u");
    S(w, "{}a"); E(w, "{}a");
    S(w, "b"); E(w, "b");
    E(w, "r");
    CHECK(out_is(w, "<r xmlns=\"urn:u\"><a xmlns=\"\"/><b/></r>"));

    /* The xml prefix is predeclared. */
    w = X();
    S(w, "r");
    char lang[96];
    snprintf(lang, sizeof lang, "{%s}lang", XMLNS_XML);
    A(w, lang, "en");
    E(w, "r");
    CHECK(out_is(w, "<r xml:lang=\"en\"/>"));

    /* A shadowed prefix is not reused. */
    w = X();
    S(w, "r"); A(w, "xmlns:p", "urn:u");
    S(w, "s"); A(w, "xmlns:p", "urn:other"); A(w, "{urn:u}k", "1"); E(w, "s");
    E(w, "r");
    CHECK(out_is(w, "<r xmlns:p=\"urn:u\"><s xmlns:p=\"urn:other\" xmlns:ns1=\"urn:u\" ns1:k=\"1\"/></r>"));

    /* The URI is escaped in the declaration it lands in. */
    w = X();
    S(w, "{urn:a&\"b}r"); E(w, "{urn:a&\"b}r");
    CHECK(out_is(w, "<r xmlns=\"urn:a&amp;&quot;b\"/>"));
}

static void test_xml_refusals(void) {
    turdom_writer *w;

    /* Balance: the refusal comes from the call, sticks, and finish says so. */
    w = X();
    S(w, "a");
    CHECK(E(w, "b") == TURDOM_E_HIERARCHY);
    CHECK(T(w, "x") == TURDOM_E_HIERARCHY);
    CHECK(E(w, "a") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a"); E(w, "a");
    CHECK(S(w, "b") == TURDOM_E_HIERARCHY);         /* a second root */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X();
    CHECK(T(w, "x") == TURDOM_E_HIERARCHY);         /* text outside the root */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a"); E(w, "a");
    CHECK(T(w, "\n") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a"); S(w, "b");
    CHECK(refused(w, TURDOM_E_HIERARCHY));          /* still open */

    w = X(); C(w, "only a comment");
    CHECK(refused(w, TURDOM_E_HIERARCHY));          /* no root */

    w = X(); S(w, "a"); T(w, "x");
    CHECK(A(w, "k", "v") == TURDOM_E_HIERARCHY);    /* attr after content */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X();
    CHECK(A(w, "k", "v") == TURDOM_E_HIERARCHY);    /* attr with no element */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(S(w, "b") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(E(w, "a") == TURDOM_E_HIERARCHY);         /* CDATA still open */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X(); S(w, "a");
    CHECK(turdom_writer_cdata(w, 0) == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    w = X();
    CHECK(turdom_writer_cdata(w, 1) == TURDOM_E_HIERARCHY);   /* outside the root */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    /* Doctypes: one, before the root. */
    w = X(); S(w, "a"); E(w, "a");
    CHECK(turdom_writer_doctype(w, "a", NULL, NULL) == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = X(); turdom_writer_doctype(w, "a", NULL, NULL);
    CHECK(turdom_writer_doctype(w, "a", NULL, NULL) == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = X();
    CHECK(turdom_writer_doctype(w, "a", "<bad>", NULL) == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X();
    CHECK(turdom_writer_doctype(w, "a", NULL, "both\"'") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X();
    CHECK(turdom_writer_doctype(w, "a b", NULL, NULL) == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));

    /* Names. */
    const char *bad_names[] = {"", "1a", "a b", "a>", "{urn:u}a:b", "{urn:u",
                               "{urn:u}", "{http://www.w3.org/2000/xmlns/}p",
                               "\xff", NULL};
    for (const char **n = bad_names; *n; n++) {
        w = X();
        CHECK(S(w, *n) == TURDOM_E_BAD_NAME);
        CHECK(refused(w, TURDOM_E_BAD_NAME));
        w = X(); S(w, "a");
        CHECK(A(w, *n, "v") == TURDOM_E_BAD_NAME);
        CHECK(refused(w, TURDOM_E_BAD_NAME));
    }
    w = X(); S(w, "a");
    CHECK(E(w, "{urn:u}a") == TURDOM_E_HIERARCHY);  /* end names match as given */
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    /* Attribute values and text are XML Chars. */
    w = X(); S(w, "a");
    CHECK(A(w, "k", "\x01") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X(); S(w, "a");
    CHECK(T(w, "\xc3") == TURDOM_E_BAD_CONTENT);    /* truncated UTF-8 */
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));

    /* Comments, PIs, CDATA: what cannot be written back as it was given. */
    const char *bad_comments[] = {"a--b", "a-", "--", "a\rb", "\x02", NULL};
    for (const char **c = bad_comments; *c; c++) {
        w = X(); S(w, "a");
        CHECK(C(w, *c) == TURDOM_E_BAD_CONTENT);
        CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    }
    w = X(); S(w, "a");
    CHECK(turdom_writer_pi(w, "xml", "v") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a");
    CHECK(turdom_writer_pi(w, "a:b", "v") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a");
    CHECK(turdom_writer_pi(w, "p", "x?>y") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X(); S(w, "a");
    CHECK(turdom_writer_pi(w, "p", " lead") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(T(w, "a]]>b") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(T(w, "a]") == TURDOM_OK);
    CHECK(T(w, "]") == TURDOM_OK);
    CHECK(T(w, ">b") == TURDOM_E_BAD_CONTENT);      /* `]]>` across calls */
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(T(w, "]]]x>") == TURDOM_OK);              /* no `]]>` in there */
    turdom_writer_cdata(w, 0); E(w, "a");
    CHECK(out_is(w, "<a><![CDATA[]]]x>]]></a>"));
    w = X(); S(w, "a"); turdom_writer_cdata(w, 1);
    CHECK(T(w, "a\rb") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));

    /* Duplicate attributes, as given or once resolved. */
    w = X(); S(w, "a"); A(w, "k", "1"); A(w, "k", "2");
    CHECK(E(w, "a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a"); A(w, "xmlns:p", "urn:u"); A(w, "p:k", "1"); A(w, "{urn:u}k", "2");
    CHECK(E(w, "a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a"); A(w, "{urn:u}k", "1"); A(w, "{urn:u}k", "2");
    CHECK(T(w, "x") == TURDOM_E_BAD_NAME);          /* refused when the tag closes */
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a"); A(w, "xmlns", "urn:u"); A(w, "xmlns", "urn:u");
    CHECK(E(w, "a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "a"); A(w, "{}xmlns", "urn:u");   /* would be a declaration */
    CHECK(E(w, "a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = X(); S(w, "{}a"); A(w, "xmlns", "urn:u");   /* its own default contradicts it */
    CHECK(E(w, "{}a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));

    /* After finish: events are refused without spoiling the output. */
    w = X(); S(w, "a"); E(w, "a");
    size_t len = 0;
    int rc = -1;
    char *s1 = turdom_writer_finish(w, &len, &rc);
    CHECK(s1 && rc == TURDOM_OK && strcmp(s1, "<a/>") == 0 && len == 4);
    CHECK(C(w, "late") == TURDOM_E_HIERARCHY);
    CHECK(turdom_writer_error(w) == TURDOM_OK);
    char *s2 = turdom_writer_finish(w, &len, &rc);
    CHECK(s2 && rc == TURDOM_OK && strcmp(s2, "<a/>") == 0);
    free(s1);
    free(s2);
    turdom_writer_free(w);

    /* Misuse that is not a refusal of the document. */
    CHECK(turdom_writer_start(NULL, "a") == TURDOM_E_BAD_KIND);
    w = X();
    CHECK(turdom_writer_start(w, NULL) == TURDOM_E_BAD_KIND);
    CHECK(turdom_writer_error(w) == TURDOM_OK);
    turdom_writer_free(w);
    turdom_writer_free(NULL);
}

static void test_html(void) {
    turdom_writer *w = H();
    S(w, "html"); S(w, "body");
    S(w, "p"); A(w, "class", "a\"b&\xc2\xa0<");
    T(w, "x<y&\xc2\xa0>\"\r");
    E(w, "p");
    S(w, "br");
    T(w, "z");
    E(w, "body");
    CHECK(out_is(w, "<html><body><p class=\"a&quot;b&amp;&nbsp;&lt;\">x&lt;y&amp;&nbsp;&gt;\"&#13;</p>"
                    "<br>z</body>"));

    /* Void elements: no frame; an end tag directly after the start is the
     * self-closed shape. */
    w = H();
    S(w, "br"); E(w, "br");
    S(w, "img"); A(w, "src", "a.png"); E(w, "img");
    S(w, "br"); T(w, "x"); E(w, "br");
    S(w, "div"); E(w, "div");
    S(w, "path"); E(w, "path");
    CHECK(out_is(w, "<br/><img src=\"a.png\"/><br>x</br><div></div><path></path>"));

    /* Raw text is written as it is, after the breakout check. */
    w = H();
    S(w, "script"); A(w, "src", "x");
    T(w, "if (a<b && c) {}");
    T(w, " </p>");
    E(w, "script");
    S(w, "STYLE"); T(w, "a > b {}"); E(w, "style");
    S(w, "xmp"); E(w, "xmp");
    S(w, "title"); T(w, "</title>"); E(w, "title");   /* RCDATA: escaped */
    CHECK(out_is(w, "<script src=\"x\">if (a<b && c) {} </p></script>"
                    "<STYLE>a > b {}</style><xmp></xmp><title>&lt;/title&gt;</title>"));

    w = H(); S(w, "script"); T(w, "x</scr"); T(w, "IPT>");
    CHECK(E(w, "script") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = H(); S(w, "script"); T(w, "<!--");
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));        /* checked at finish too */
    w = H(); S(w, "style"); T(w, "</style");
    CHECK(E(w, "style") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = H(); S(w, "style"); T(w, "a\rb");
    CHECK(E(w, "style") == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = H(); S(w, "script"); T(w, "x");
    CHECK(out_is(w, "<script>x"));                  /* raw text flushed at finish */

    w = H(); S(w, "script");
    CHECK(S(w, "b") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = H(); S(w, "script");
    CHECK(C(w, "c") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = H(); S(w, "script");
    CHECK(turdom_writer_doctype(w, "html", NULL, NULL) == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = H(); S(w, "script");
    CHECK(E(w, "style") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));

    /* End tags need not balance; they close the nearest match. */
    w = H();
    S(w, "a"); S(w, "b");
    E(w, "a");
    E(w, "c");
    S(w, "script"); T(w, "</b>"); E(w, "script");
    CHECK(out_is(w, "<a><b></a></c><script></b></script>"));

    /* Doctypes keep their ids, and may come anywhere. */
    w = H();
    turdom_writer_doctype(w, "html", "-//W3C//DTD HTML 4.01//EN", "http://x/y.dtd");
    S(w, "p");
    turdom_writer_doctype(w, "html", "p'q", NULL);
    turdom_writer_doctype(w, "html", NULL, "s");
    turdom_writer_doctype(w, "", NULL, NULL);
    CHECK(out_is(w, "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01//EN\" \"http://x/y.dtd\"><p>"
                    "<!DOCTYPE html PUBLIC \"p'q\"><!DOCTYPE html SYSTEM \"s\"><!DOCTYPE >"));
    w = H();
    CHECK(turdom_writer_doctype(w, "html", "a>b", NULL) == TURDOM_E_BAD_CONTENT);
    CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    w = H();
    CHECK(turdom_writer_doctype(w, "", "p", NULL) == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = H();
    CHECK(turdom_writer_doctype(w, "a b", NULL, NULL) == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));

    /* Refusals. */
    w = H();
    CHECK(S(w, "plaintext") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = H();
    CHECK(S(w, "1a") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = H(); S(w, "p");
    CHECK(A(w, "on=x", "1") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = H(); S(w, "p"); A(w, "id", "1"); A(w, "ID", "2");
    CHECK(T(w, "x") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
    w = H(); S(w, "p");
    CHECK(turdom_writer_cdata(w, 1) == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    w = H(); S(w, "p");
    CHECK(turdom_writer_pi(w, "p", "d") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    const char *bad_comments[] = {"a-->", "a--!>b", "<!--x", ">x", "->x", "x<!-", "a\rb", NULL};
    for (const char **c = bad_comments; *c; c++) {
        w = H();
        CHECK(C(w, *c) == TURDOM_E_BAD_CONTENT);
        CHECK(refused(w, TURDOM_E_BAD_CONTENT));
    }
    w = H(); C(w, "a--b"); C(w, "x-"); C(w, "");
    CHECK(out_is(w, "<!--a--b--><!--x---><!---->"));

    /* Unclosed elements are fine; a held start tag is closed at finish. */
    w = H(); S(w, "p"); A(w, "a", "1");
    CHECK(out_is(w, "<p a=\"1\">"));
    w = H();
    CHECK(out_is(w, ""));
}

static int file_is(const char *path, const char *want, size_t wlen) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    char *buf = (char *)malloc(wlen + 2);
    size_t got = fread(buf, 1, wlen + 1, f);
    fclose(f);
    int ok = got == wlen && memcmp(buf, want, wlen) == 0;
    free(buf);
    return ok;
}

static void test_file_sink(const char *dir) {
    char path[4096];
    snprintf(path, sizeof path, "%s/writer-out.xml", dir);

    /* 300 KiB of text crosses the 64 KiB spill several times. */
    size_t n = 300 * 1024;
    char *big = (char *)malloc(n + 1);
    for (size_t i = 0; i < n; i++) big[i] = (char)('a' + (int)(i % 26));
    big[n] = '\0';
    char *want = (char *)malloc(n + 64);
    snprintf(want, n + 64, "<r a=\"1\">%s<e/></r>", big);

    turdom_writer *w = X();
    CHECK(turdom_writer_to_file(w, path) == TURDOM_OK);
    CHECK(turdom_writer_to_file(w, path) == TURDOM_E_HIERARCHY);   /* only once */
    S(w, "r"); A(w, "a", "1");
    for (size_t i = 0; i < n; i += 1000)
        CHECK(turdom_writer_chars(w, big + i, n - i < 1000 ? n - i : 1000) == TURDOM_OK);
    S(w, "e"); E(w, "e"); E(w, "r");
    size_t len = 7;
    int rc = -1;
    char *s = turdom_writer_finish(w, &len, &rc);
    CHECK(s && rc == TURDOM_OK && len == 0 && s[0] == '\0');
    free(s);
    turdom_writer_free(w);
    CHECK(file_is(path, want, strlen(want)));

    /* Not after the first event. */
    w = X(); S(w, "r");
    CHECK(turdom_writer_to_file(w, path) == TURDOM_E_HIERARCHY);
    turdom_writer_free(w);

    /* A path that cannot be created: not sticky, the writer still works. */
    w = X();
    CHECK(turdom_writer_to_file(w, "/nonexistent-dir-for-turdom/x.xml") == TURDOM_E_IO);
    CHECK(turdom_writer_error(w) == TURDOM_OK);
    S(w, "r"); E(w, "r");
    CHECK(out_is(w, "<r/>"));

    /* A refused document: freed without finishing, the file is closed. */
    w = X();
    CHECK(turdom_writer_to_file(w, path) == TURDOM_OK);
    S(w, "r");
    CHECK(E(w, "x") == TURDOM_E_HIERARCHY);
    CHECK(refused(w, TURDOM_E_HIERARCHY));
    remove(path);
    free(big);
    free(want);
}

static void test_big(void) {
    /* 100k levels of Clark names: one declaration, at the root. */
    const int depth = 100000;
    turdom_writer *w = X();
    int ok = 1;
    for (int i = 0; i < depth && ok; i++) ok = S(w, "{urn:u}d") == TURDOM_OK;
    for (int i = 0; i < depth && ok; i++) ok = E(w, "{urn:u}d") == TURDOM_OK;
    CHECK(ok);
    size_t len = 0;
    int rc = -1;
    char *s = turdom_writer_finish(w, &len, &rc);
    size_t want = strlen("<d xmlns=\"urn:u\">") + (size_t)(depth - 2) * strlen("<d></d>") +
                  strlen("<d/>") + strlen("</d>");
    CHECK(s && rc == TURDOM_OK && len == want);
    CHECK(s && strncmp(s, "<d xmlns=\"urn:u\"><d><d>", 23) == 0);
    free(s);
    turdom_writer_free(w);

    /* 20k attributes on one element: the duplicate check is not quadratic,
     * and a duplicate at the far end is still found. */
    w = X();
    S(w, "r");
    char name[32];
    for (int i = 0; i < 20000; i++) {
        snprintf(name, sizeof name, "a%d", i);
        A(w, name, "v");
    }
    E(w, "r");
    s = turdom_writer_finish(w, &len, &rc);
    CHECK(s && rc == TURDOM_OK && strncmp(s, "<r a0=\"v\" a1=\"v\" ", 17) == 0);
    free(s);
    turdom_writer_free(w);
    w = X();
    S(w, "r");
    for (int i = 0; i < 20000; i++) {
        snprintf(name, sizeof name, "a%d", i);
        A(w, name, "v");
    }
    A(w, "a0", "again");
    CHECK(E(w, "r") == TURDOM_E_BAD_NAME);
    CHECK(refused(w, TURDOM_E_BAD_NAME));
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : ".";
    test_xml_basic();
    test_xml_clark();
    test_xml_refusals();
    test_html();
    test_file_sink(dir);
    test_big();
    if (g_fails) {
        printf("# %d of %d checks failed\n", g_fails, g_checks);
        return 1;
    }
    printf("ok - turdom writer: %d checks\n", g_checks);
    return 0;
}
