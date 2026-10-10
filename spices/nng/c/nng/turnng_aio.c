/* turnng_aio.c -- the spice-owned box behind `nng/aio`'s `Aio`.
 *
 * One box wraps one nng_aio and adds the three things the Turmeric surface
 * promises on top of nng's own contract:
 *
 *   1. No nng_msg ever reaches Turmeric code or leaks.  A failed send leaves
 *      its message with the caller (nng_send_aio.3); the completion callback
 *      frees it.  An untaken receive is freed by the next submit or by free.
 *   2. A busy aio is never resubmitted (nng asserts on that, and is undefined
 *      in a release build): every submit refuses with NNG_EBUSY instead.
 *   3. A completion can be WAITED FOR by a reactor or a fiber, not only by a
 *      blocking nng_aio_wait: the callback writes one byte to a pipe whose
 *      read end is aio-poll-fd.  The pipe is created lazily, on the first
 *      aio-poll-fd, so an Aio that is only ever waited on costs no fds.
 *
 * The completion callback runs on an nng worker thread with no locks held.
 * It touches only this box -- never a Turmeric closure, which must not run
 * off its own thread.  The poll fd is readable from completion until the
 * result is collected (turnng_aio_wait, a turnng_aio_try_result that sees the
 * operation finished, a successful take) or the next submit, so a reactor
 * callback neither spins on epoll nor stalls on kqueue.
 *
 * Like turnng_payload.c this file is compiled on the same command line as the
 * emitted C; unlike it, it needs <nng/nng.h>, which the fetched dep's include
 * dir supplies.
 */

#include <nng/nng.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern void *turnng_payload_of_bytes(const void *src, int64_t n);

enum { TURNNG_OP_NONE = 0, TURNNG_OP_SEND = 1, TURNNG_OP_RECV = 2 };

typedef struct turnng_aio {
    nng_aio        *aio;
    pthread_mutex_t mu;
    int             op;         /* the operation in flight / last submitted */
    int             signalled;  /* completed, not yet collected or re-armed */
    int             rfd, wfd;   /* completion pipe; -1 until turnng_aio_fd */
    nng_duration    timeout;    /* re-applied on every submit; see arm */
} turnng_aio;

/* Runs on an nng worker thread.  Touches only the box. */
static void turnng_aio_cb(void *arg) {
    turnng_aio *a = (turnng_aio *)arg;
    if (a->op == TURNNG_OP_SEND && nng_aio_result(a->aio) != 0) {
        nng_msg *m = nng_aio_get_msg(a->aio);
        if (m) { nng_aio_set_msg(a->aio, NULL); nng_msg_free(m); }
    }
    pthread_mutex_lock(&a->mu);
    a->signalled = 1;
    if (a->wfd >= 0) { char b = 1; ssize_t w = write(a->wfd, &b, 1); (void)w; }
    pthread_mutex_unlock(&a->mu);
}

static void turnng_aio_drop_msg(turnng_aio *a) {
    nng_msg *m = nng_aio_get_msg(a->aio);
    if (m) { nng_aio_set_msg(a->aio, NULL); nng_msg_free(m); }
}

/* Clear the completion signal.  Only called when the operation is not busy,
 * so the callback has already run and cannot write after this. */
static void turnng_aio_clear(turnng_aio *a) {
    pthread_mutex_lock(&a->mu);
    if (a->signalled && a->rfd >= 0) {
        char buf[16];
        while (read(a->rfd, buf, sizeof buf) > 0) { }
    }
    a->signalled = 0;
    pthread_mutex_unlock(&a->mu);
}

void *turnng_aio_new(int *rv_out) {
    turnng_aio *a = (turnng_aio *)calloc(1, sizeof *a);
    if (!a) { *rv_out = NNG_ENOMEM; return NULL; }
    a->rfd = a->wfd = -1;
    a->timeout = NNG_DURATION_DEFAULT;
    pthread_mutex_init(&a->mu, NULL);
    int rv = nng_aio_alloc(&a->aio, turnng_aio_cb, a);
    if (rv != 0) {
        pthread_mutex_destroy(&a->mu);
        free(a);
        *rv_out = rv;
        return NULL;
    }
    *rv_out = 0;
    return a;
}

void turnng_aio_free(void *p) {
    turnng_aio *a = (turnng_aio *)p;
    if (!a) return;
    /* Stop first: it cancels an in-flight operation and waits for the
     * callback, which touches mu and wfd -- so only after it may they go. */
    nng_aio_stop(a->aio);
    turnng_aio_drop_msg(a);        /* an untaken receive */
    nng_aio_free(a->aio);
    if (a->rfd >= 0) { close(a->rfd); close(a->wfd); }
    pthread_mutex_destroy(&a->mu);
    free(a);
}

/* Every submit: refuse a busy aio, free an untaken receive, clear the
 * completion signal, record the operation, re-apply the slot's timeout.
 *
 * The last step is what makes "the default inherits the socket's timeout"
 * true on every submit.  nng resolves NNG_DURATION_DEFAULT by overwriting the
 * aio's timeout with the socket's on the first submit, so without it a
 * reused slot would keep the first socket's value for good -- and a slot
 * first used while the socket waited forever would never time out later. */
static int turnng_aio_arm(turnng_aio *a, int op) {
    if (nng_aio_busy(a->aio)) return NNG_EBUSY;
    turnng_aio_drop_msg(a);
    turnng_aio_clear(a);
    a->op = op;
    nng_aio_set_timeout(a->aio, a->timeout);
    return 0;
}

int turnng_aio_fd(void *p, int *fd_out) {
    turnng_aio *a = (turnng_aio *)p;
    pthread_mutex_lock(&a->mu);
    if (a->rfd < 0) {
        int fds[2];
        if (pipe(fds) != 0) { pthread_mutex_unlock(&a->mu); return NNG_ENOMEM; }
        for (int i = 0; i < 2; i++) {
            fcntl(fds[i], F_SETFL, fcntl(fds[i], F_GETFL) | O_NONBLOCK);
            fcntl(fds[i], F_SETFD, FD_CLOEXEC);
        }
        a->rfd = fds[0];
        a->wfd = fds[1];
        /* Already complete: raise it now, so a late registration still sees
         * the completion. */
        if (a->signalled) { char b = 1; ssize_t w = write(a->wfd, &b, 1); (void)w; }
    }
    *fd_out = a->rfd;
    pthread_mutex_unlock(&a->mu);
    return 0;
}

/* Submit a send of `len` bytes, on a socket (use_ctx == 0) or a context. */
int turnng_aio_send_bytes(void *p, uint32_t sock_id, uint32_t ctx_id,
                          int use_ctx, const void *data, size_t len) {
    turnng_aio *a = (turnng_aio *)p;
    int rv = turnng_aio_arm(a, TURNNG_OP_SEND);
    if (rv != 0) return rv;
    nng_msg *m = NULL;
    if ((rv = nng_msg_alloc(&m, 0)) != 0) return rv;
    if (len > 0 && (rv = nng_msg_append(m, data, len)) != 0) {
        nng_msg_free(m);
        return rv;
    }
    nng_aio_set_msg(a->aio, m);
    if (use_ctx) { nng_ctx c; c.id = ctx_id; nng_ctx_send(c, a->aio); }
    else         { nng_socket s; s.id = sock_id; nng_sock_send(s, a->aio); }
    return 0;
}

int turnng_aio_recv(void *p, uint32_t sock_id, uint32_t ctx_id, int use_ctx) {
    turnng_aio *a = (turnng_aio *)p;
    int rv = turnng_aio_arm(a, TURNNG_OP_RECV);
    if (rv != 0) return rv;
    if (use_ctx) { nng_ctx c; c.id = ctx_id; nng_ctx_recv(c, a->aio); }
    else         { nng_socket s; s.id = sock_id; nng_sock_recv(s, a->aio); }
    return 0;
}

/* Block until complete; the operation's nng result (0 = ok).  Collects. */
int turnng_aio_wait(void *p) {
    turnng_aio *a = (turnng_aio *)p;
    nng_aio_wait(a->aio);
    turnng_aio_clear(a);
    return nng_aio_result(a->aio);
}

/* -1 while in flight; otherwise the result (0 = ok), and collects. */
int turnng_aio_try_result(void *p) {
    turnng_aio *a = (turnng_aio *)p;
    if (nng_aio_busy(a->aio)) return -1;
    turnng_aio_clear(a);
    return nng_aio_result(a->aio);
}

void turnng_aio_cancel(void *p) {
    nng_aio_cancel(((turnng_aio *)p)->aio);
}

/* Recorded, and applied by the next submit (see arm). */
void turnng_aio_set_timeout(void *p, int64_t ms) {
    ((turnng_aio *)p)->timeout = (nng_duration)ms;
}

/* The received message, detached from the aio: NNG_ESTATE when there is
 * nothing to take (not a receive, still in flight, already taken), the
 * receive's own error when it failed.  On success *msg_out is owned by the
 * caller and the completion is collected. */
static int turnng_aio_take(turnng_aio *a, nng_msg **msg_out) {
    if (a->op != TURNNG_OP_RECV || nng_aio_busy(a->aio)) return NNG_ESTATE;
    /* The receive has finished, so this take observes its result: collect
     * whether it succeeded or not, or a reactor watching the poll fd would
     * spin on a failed receive nobody else collects. */
    turnng_aio_clear(a);
    int rv = nng_aio_result(a->aio);
    if (rv != 0) return rv;
    nng_msg *m = nng_aio_get_msg(a->aio);
    if (!m) return NNG_ESTATE;
    nng_aio_set_msg(a->aio, NULL);
    *msg_out = m;
    return 0;
}

int turnng_aio_take_payload(void *p, void **out) {
    nng_msg *m = NULL;
    int rv = turnng_aio_take((turnng_aio *)p, &m);
    if (rv != 0) return rv;
    size_t len = nng_msg_len(m);
    void *b = turnng_payload_of_bytes(len ? nng_msg_body(m) : "", (int64_t)len);
    nng_msg_free(m);
    if (!b) return NNG_ENOMEM;
    *out = b;
    return 0;
}

/* A NUL-terminated copy: a payload with an interior NUL reads back truncated,
 * the same limit as recv-str. */
int turnng_aio_take_str(void *p, char **out) {
    nng_msg *m = NULL;
    int rv = turnng_aio_take((turnng_aio *)p, &m);
    if (rv != 0) return rv;
    size_t len = nng_msg_len(m);
    char *s = (char *)malloc(len + 1);
    if (!s) { nng_msg_free(m); return NNG_ENOMEM; }
    if (len > 0) memcpy(s, nng_msg_body(m), len);
    s[len] = '\0';
    nng_msg_free(m);
    *out = s;
    return 0;
}
