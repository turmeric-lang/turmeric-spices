---
title: Secrets and Crypto Hygiene
category: Security
description: Linear key material with tur-secret -- a wipe the optimizer cannot delete, an OS CSPRNG, constant-time comparison, and a Secret type whose destructor the compiler enforces
---

# tur-secret guide

`tur-secret` covers the handling of key material: getting it, holding it,
comparing it, and destroying it. This guide is in two halves -- how to use it,
then how it works and why it is built the way it is. The second half matters
more than usual here, because most of the value in a spice like this is in
choices you cannot see from the API.

Design plan:
[`docs/upcoming/secret-spice-plan.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/upcoming/secret-spice-plan.md)
in the turmeric repo.

## Why it exists

Turmeric's standard library has no security-grade primitives, and the gaps
are not obvious from the names:

| You reach for | You get | Consequence |
|---|---|---|
| `stdlib/random.tur` | `rand()` seeded `time(NULL)` | a few thousand possible token streams |
| `raw-memset` | plain `memset` | deleted by the optimizer on a dying buffer |
| `stdlib/digest.tur` | SHA-256, no HMAC | raw hashes invite length-extension misuse |
| `=` on two buffers | `memcmp` | timing reveals the shared-prefix length |

None of these are bugs. They are correct implementations of things that are
not what you want when an attacker is in the picture. This spice provides the
versions that are.

## Getting it

`secret` is a workspace member, so a sibling spice in this repo can import it
with no fetch step. To declare it explicitly:

```turmeric no-check
:spices #map{
  "secret" #map{:path "../secret"}
}
```

Four public modules:

- `secret/hygiene` -- three primitives that work on any raw buffer.
- `secret/core` -- the linear `Secret` handle built on top of them.
- `secret/kdf` -- HMAC-SHA256 and HKDF-SHA256 over `Secret`s.
- `secret/hex` -- constant-time hex codecs whose output is a `Secret`.

(`secret/internal` also exists; it is intra-spice plumbing and not part of
the public API.)

Use `secret/hygiene` directly when you are dealing with someone else's buffer
(a decoded header, a socket read). Use `secret/core` when you own the
material and want the compiler to hold you to destroying it.

---

# Part 1 -- Usage

## The hygiene floor

```turmeric
(import secret/hygiene :refer [secure-wipe-ptr! crypto-random-bytes!
                               crypto-random-source ct-eq-ptr?])
```

```sweet-exp
import secret/hygiene :refer [secure-wipe-ptr! crypto-random-bytes!
                              crypto-random-source ct-eq-ptr?]
```

### Getting entropy

```turmeric no-check
(crypto-random-bytes! p n)   ;; : (Result int cstr)
(crypto-random-source)       ;; : cstr -- names the backend
```

`crypto-random-bytes!` fills `n` bytes at `p` from the OS CSPRNG and returns
the count on success. On failure it wipes the destination rather than leaving
it partly filled, so a caller who ignores the `Result` ends up with zeros --
obviously broken -- instead of a short, low-entropy "key" that works.

`crypto-random-source` reports which backend the build selected
(`arc4random_buf`, `getrandom`, `BCryptGenRandom`, `/dev/urandom`). It is
worth logging at startup: it is how you find out you are on a weaker path
than you assumed.

### Comparing secrets

```turmeric no-check
(ct-eq-ptr? a b n)   ;; : bool -- constant time in the buffers' contents
```

Use this for MACs, session tokens, password hashes, and anything else an
attacker submits guesses against. `memcmp` returns at the first differing
byte, so its runtime tells the attacker how long a prefix they got right,
which turns an infeasible search into a linear one.

The timing is independent of the buffers' *contents*, not their *lengths*. To
compare variable-length secrets, compare fixed-size digests of them instead.

### Destroying secrets

```turmeric no-check
(secure-wipe-ptr! p n)   ;; : void
```

Use this and never `raw-memset` for key material. See
[Why a wipe needs help](#why-a-wipe-needs-help) -- this is not a
micro-optimization, it is the difference between the bytes being gone and
still being there.

## The linear `Secret`

`Secret` is the part that has no equivalent in Go, Rust `zeroize`, or
libsodium: a value the **compiler** will not let you forget to destroy.

```turmeric
(import secret/core :refer [Secret secret-random! secret-wipe!
                            with-secret secret-len secret-eq?])
(import secret/hygiene :refer [ct-eq-ptr?])

(defn derive-and-check [supplied : ptr<void> n : int] : bool
  (let [r (secret-random! 32)]
    (if (ok? r)
      (let [k  (ok-val r)
            eq (with-secret k (fn [p : ptr<void> m : int] : int
                                (if (ct-eq-ptr? p supplied m) 1 0)))]
        (do
          (secret-wipe! k)
          (= eq 1)))
      false)))
```

```sweet-exp
import secret/core :refer [Secret secret-random! secret-wipe!
                           with-secret secret-len secret-eq?]
import secret/hygiene :refer [ct-eq-ptr?]

defn derive-and-check [supplied : ptr<void> n : int] : bool
  let [r secret-random!(32)]
    if ok?(r)
      let [k  ok-val(r)
           eq with-secret(k (fn [p : ptr<void> m : int] : int
                              (if (ct-eq-ptr? p supplied m) 1 0)))]
        do
          secret-wipe!(k)
          =(eq 1)
      false
```

Delete the `(secret-wipe! k)` line and the program stops compiling:

```
error [TUR-E0100]: linear value 'k' dropped without being consumed
```

### The lifecycle

A `Secret` has exactly one legal shape of life:

```
  secret-random!  --+
                    +--> [ live ] --+--> secret-wipe!   (consumes; exactly once)
  secret-of-bytes --+       ^       |
                            |       +--> TUR-E0100 if this path is missing
                            |
             borrowed by with-secret / secret-len /
             secret-locked? / secret-eq? -- any number
             of times, none of them consuming
```

Three mistakes are compile errors rather than runtime hazards:

| Mistake | Diagnostic |
|---|---|
| never wiping | `TUR-E0100` linear value dropped without being consumed |
| reading after wiping | `TUR-E0101` linear value used after being consumed |
| wiping twice | `TUR-E0101` linear value used after being consumed |

**This is on by default.** As of `tur` 0.35, `-Xsubstructural` is a no-op that
warns `TUR-W0050`. You do not have to opt in, and you cannot accidentally
build without it.

The observing operations take the secret by `^borrow`, which means they look
at it without discharging the obligation -- so a long function that reads a
key five times still owes exactly one `secret-wipe!`.

### Constructing from existing bytes

```turmeric no-check
(secret-of-bytes p n)   ;; : (Result Secret cstr) -- and wipes p
```

`secret-of-bytes` **wipes the caller's buffer**. That is the reason it exists
instead of a plain copy: material that arrived in an ordinary buffer lives in
two places until the plain copy is destroyed, and the caller who has just
handed ownership over is the one least likely to remember the copy is still
there. Handing ownership over should destroy your copy, so it does.

### Checking that locking worked

```turmeric no-check
(secret-locked? k)   ;; : bool
```

Page locking is fail-soft (see [Locking](#locking-and-fail-soft) below). If
your threat model actually requires the key to stay out of swap, ask.

## Deriving and authenticating

```turmeric no-check
(secret-hmac-sha256 key p n)                  ;; : (Result Secret cstr)
(secret-hkdf-sha256 ikm salt info n)          ;; : (Result Secret cstr)
```

Both hand back a `Secret`, so a derived key inherits the same discipline as
a minted one. The MAC is a `Secret` too, deliberately: a MAC compared with
`memcmp` is as dangerous as a leaked key, and keeping it in a `Secret` makes
`secret-eq?` -- which is constant-time -- the path of least resistance.

`salt` and `info` are `cstr`, measured with `strlen`. That fits the usual
case (ASCII labels like `"v1"` or `"encryption key"`) but cannot carry an
embedded NUL, so a binary salt needs `hkdf-sha256-raw!`, which takes
pointer/length pairs. RFC 5869's own first test vector uses the salt
`00 01 .. 0c`, so this is not a hypothetical gap.

## Hex at the boundary

```turmeric no-check
(secret-of-hex "deadbeef")   ;; : (Result Secret cstr) -- 4 bytes
(secret->hex k)              ;; : (Result Secret cstr) -- 2n hex chars
```

Hex is how key material actually travels: config files, environment
variables, API responses. Encoding returns a `Secret` because the hex form of
a key is still the key -- returning a plain `cstr` would hand back an unwiped
copy and quietly undo the discipline the caller opted into. Decoding returns
one because material arriving as hex is exactly the material most likely to
be left lying around.

`secret-of-hex` does not wipe its source string; that buffer's lifetime
belongs to the caller. If it holds real key material, follow up with
`secure-wipe-ptr!`.

---

# Part 2 -- Implementation

## Why a wipe needs help

`memset(p, 0, n)` on a buffer nothing reads afterwards is a *dead store*. The
C standard lets the compiler delete it, and compilers do. The buffer you
"cleared" still holds the key.

This spice's `-O2` fixture measures it rather than asserting it. It fills a
512-byte stack frame with 64 copies of a magic pattern, wipes it, returns,
then has another function claim a frame at the same depth and count surviving
copies:

```
$ bash tests/o2/run.sh
stack-residue probe (built with -O2):
  no wipe (positive ctl)   residue=64
  plain memset (control)   residue=64
  secure-wipe-ptr!         residue=0
```

Apple clang at `-O2` deleted the `memset` outright -- the control is
indistinguishable from not wiping at all. That is the failure mode, observed,
on the toolchain most readers of this guide are using.

### The ladder

`secure-wipe-ptr!` picks the best primitive the platform offers:

1. `SecureZeroMemory` -- Win32; documented never to be elided.
2. `explicit_bzero` -- glibc >= 2.25, OpenBSD, FreeBSD, NetBSD, DragonFly.
3. A `volatile` function pointer to `memset` -- everywhere else.

Then a compiler memory barrier (`__asm__ __volatile__("" ::: "memory")`) on
GCC/clang, so the wipe cannot be sunk past a later use.

**macOS lands on rung 3.** Apple's libc ships neither `explicit_bzero` nor an
unguarded `memset_s` (`memset_s` requires `__STDC_WANT_LIB_EXT1__` to be
defined before `<string.h>`, which an inline-C body cannot guarantee, because
includes are hoisted). Rung 3 is not a weak fallback -- it is what libsodium
uses for the same reason:

```c
static void *(*volatile vmemset)(void *, int, size_t) = memset;
vmemset(p, 0, len);
```

The `volatile` is on the **pointer**, not the pointee. The compiler must
reload it and cannot prove which function it points at, so the call has to be
emitted. The measured `residue=0` above is this rung doing its job.

## The CSPRNG ladder

`BCryptGenRandom` -> `arc4random_buf` -> `getrandom(2)` -> `/dev/urandom`.

`arc4random_buf` is preferred on macOS and the BSDs because it cannot fail and
never blocks. On Linux, `getrandom(2)` is tried via `syscall(SYS_getrandom, ...)`
with `EINTR` retried, and falls through to `/dev/urandom` on `ENOSYS` (pre-3.17
kernels), which also covers any platform that reaches the bottom of the ladder.

The short-read path matters: if `/dev/urandom` returns fewer bytes than asked,
the destination is wiped and an error returned. Partial entropy is worse than
none, because it looks like it worked.

## Constant-time comparison

```c
const volatile unsigned char *x = a, *y = b;
volatile unsigned char acc = 0;
for (size_t i = 0; i < n; i++) acc |= (unsigned char)(x[i] ^ y[i]);
return acc == 0;
```

The accumulator is `volatile` so the compiler cannot introduce an early exit
once `acc` is known non-zero -- which, left to itself, is an optimization it
would be entitled to make, and which would reintroduce exactly the leak this
function exists to remove.

## The allocation shape

One allocation holds metadata and payload together:

```c
struct __tur_secret { int64_t len; int64_t locked; unsigned char data[]; };
```

One `malloc`, one `free`, and the payload pointer is always a fixed offset
from the handle. `Secret` rides the int carrier as a pointer to this struct.
It is the same shape the zlib spice's `__gzbuf` uses.

`secret-wipe!` zeroes the payload **before** unlocking the pages, because
`munlock` makes them swappable again -- unlock first and the window between
unlock and wipe is exactly the thing locking was there to prevent.

## Locking and fail-soft

At construction the whole allocation is `mlock`ed (header included -- the
kernel rounds to page granularity, so a partial lock buys nothing) and marked
`MADV_DONTDUMP` where that exists.

Locking failure is **not** fatal. `RLIMIT_MEMLOCK` is 64 KiB by default on
many Linux distributions, and a constructor that refused to mint a key over an
ambient ulimit would push callers back to raw `malloc` buffers -- strictly
worse than a locked-if-possible one. So the result is recorded and exposed via
`secret-locked?`, and callers who genuinely require locking can check.

## Why the API returns `Result`, and where it is built

Every fallible entry point returns `(Result T cstr)` -- never a sentinel
`:int`, per the project's standing rule.

But note **where** those Results are constructed: entirely in Turmeric, with
`ok`/`err`, while the inline-C helpers deal only in raw pointers and bools.

That split began as a workaround. An inline-C body returns the *boxed* Result
carrier, and the box-to-struct conversion used to be silently dropped in any
function the CPS transform touches -- which is every caller of a higher-order
function like `with-secret` -- producing a wall of C type errors with no
`.tur` attribution. That is **fixed** as of turmeric `65024bca`
([`cps-result-unbox-dropped.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/cps-result-unbox-dropped.md)),
so it is no longer forced.

The split is kept anyway, because it is the better factoring on its own
terms: inline C does pointer work, Turmeric does control flow and error
typing.

So `secret-random!` reads:

```turmeric no-check
(defn secret-random! [n : int] #fx{Rand} : (Result Secret cstr)
  (if (< n 1)
    (err "secret-random!: length must be positive")
    (let [h (secret-alloc-raw n)]
      (if (= h 0)
        (err "secret-random!: allocation failed")
        (if (secret-fill-random-raw! h n)
          (ok (secret-of-raw h))
          (do (secret-free-raw! h)
              (err "secret-random!: OS CSPRNG failed")))))))
```

Note the error path calls `secret-free-raw!` -- the same teardown
`secret-wipe!` uses. A mint whose entropy fill failed releases its pages
instead of handing back a zero-filled buffer that would pass for a key.

## Why SHA-256 is implemented here

`secret/kdf` carries its own SHA-256, HMAC, and HKDF rather than linking the
tls spice's mbedTLS. The plan originally said to reuse mbedTLS; the reasoning
for not doing so:

- S1 and S2 need no external library at all. Adding a `:cmake-deps` row makes
  every caller who wanted `secure-wipe-ptr!` clone and build mbedTLS.
- For a *security* spice, a smaller supply-chain surface is worth something on
  its own.
- The repo already ships a hand-rolled SHA-256 in `stdlib/digest.tur`, whose
  header advertises "no external crypto libraries are required."

"Don't roll your own crypto" is a real rule, and it is about protocols,
primitives with genuine side-channel subtlety (AES, RSA, elliptic curves),
and novel constructions. SHA-256 / HMAC / HKDF are the textbook exception:
fixed-size, no secret-dependent branching or indexing, no bignum arithmetic.

**That argument only holds while the vectors do.** The implementations are
pinned to FIPS 180-4 (SHA-256), RFC 4231 cases 1/2/3/6 (HMAC -- case 6 covers
the "hash a key longer than the block" branch), and RFC 5869 cases 1 and 3
(HKDF), and the hex encoder is checked exhaustively against `printf %02x` for
all 256 byte values. If those tests are ever weakened or skipped, the case
for not linking a vetted library goes with them.

Worth noting how that played out: three of the vector tests failed on first
run, and all three were transcription errors in the *test file* (a 49-byte
message where the RFC says 50, a 135-byte key where it says 131, a wrong
expected string) rather than implementation bugs. That is the argument for
writing the vectors out longhand instead of round-tripping the code against
itself -- a round-trip test would have passed against a wrong implementation.

## Constant-time hex

The obvious hex encoder indexes a 16-byte table with a nibble of the secret.
That is a secret-dependent memory access, and cache-timing attacks against
exactly that pattern are well established. Both directions here are
branchless arithmetic:

```c
/* encode: '0' + x + (39 if x > 9 else 0), 39 == 'a' - '0' - 10 */
o[i*2] = (unsigned char)('0' + hi + (((9 - hi) >> 31) & 39));
```

`9 - x` is negative exactly when `x > 9`, so the arithmetic shift yields all
ones and the mask contributes 39. No table, no branch.

Decoding uses a constant-time range test, `(((lo-1-x) & (x-hi-1)) >> 31)`,
which is `-1` when `lo <= x <= hi` and `0` otherwise. The three character
classes (`0-9`, `A-F`, `a-f`) are masked together, and validity is
accumulated across the whole string rather than short-circuited, so a bad
character does not change how long the decode takes. A failed decode wipes
its output rather than returning a partial decode of attacker-controlled
input.

What is *not* hidden: the length of the input, and whether decoding
succeeded. Neither is secret -- both are observable from the surrounding
protocol anyway.

## Naming: `-raw`, not `__`

The private helpers are `secret-alloc-raw`, `secret-free-raw!`,
`secret-data-raw` -- not the `__`-prefixed convention used elsewhere in
stdlib and the spices. Inline-C bodies call siblings through
`__TUR_CNAME_<name>__`, and a name that itself began with `__` used to break
that macro's expansion, producing a call to a function that does not exist
(or, with a hyphen in the name, a phantom `alloc__` from the C tokenizer
reading the hyphen as a minus). That is **fixed** as of turmeric `490172c6`
([`tur-cname-macro-breaks-on-leading-underscores.md`](https://github.com/turmeric-lang/turmeric/blob/main/docs/archive/tur-cname-macro-breaks-on-leading-underscores.md)),
so either spelling works now.

The `-raw` suffix is kept regardless: privacy comes from not exporting these,
which is what actually enforces it, and the suffix says what they are rather
than just that they are internal.

## Testing methodology

The suite is in three parts, and the third is the interesting one.

- `tur test tests` -- 50 functional cases, including the RFC vectors.
- `bash errors/run.sh` -- the three linear-`Secret` diagnostics. These are
  compile-*fail* fixtures, so they live outside `tests/` (`tur test` recurses
  and would try to build them as an ordinary suite). The runner asserts each
  is rejected with the right code, and was itself checked in both directions:
  handed a fixture that compiles clean, it reports `not ok` and exits 1.
- `bash tests/o2/run.sh` -- the suite rebuilt at `-O2`, plus the dead-store
  probe.

### Two drafts of the probe that passed while measuring nothing

Worth recounting, because the failure mode generalizes.

**Draft 1** called the writer and the scanner directly. Both inlined into
`main`, their arrays got *disjoint* slots in one big frame, and the scanner
never looked at the memory the writer used. Result: `residue=0` everywhere,
including the plain-`memset` control. A clean pass, measuring nothing. Fixed
by calling both through `volatile` function pointers, which forces real calls
at the same stack depth.

**Draft 2** still read the scan buffer as an ordinary uninitialized array.
Reading uninitialized memory yields `undef`, which clang at `-O2` is entitled
to fold the comparison against -- so it deleted the entire scan loop and
returned zero hits regardless of what was in the frame. Another clean pass,
still measuring nothing. Fixed by reading through a `volatile` pointer.

Both were caught by adding a **positive control**: a frame that is written and
never wiped, which *must* show residue if the probe works at all. The probe now
reports `PASS (probe blind)` rather than `PASS` when the positive control comes
back empty.

The general lesson: a security test that can only report "clean" is
indistinguishable from a test that is not running. Give it something it is
required to find.

### And the suite itself was not failing

A related trap, found the same day and worth knowing about because it applies
to every spice in the repo: `tur test` judges a file by its **exit code**, but
the conventional `main` ends in a hardcoded `0` and `run-all` returns `:void`.
So a failing `it` printed `not ok`, the summary printed "1 failed", and the
process still exited 0 -- `tur test` reported the file as passed.

It hid a genuinely failing assertion in this spice's hex tests, caught only
because the TAP lines were being read by eye. The fix is `run-all-status`
(added to the `test` spice, additive), which returns 1 when anything failed:

```turmeric no-check
(defn main [] : int
  (__all-tests)
  (run-all-status))
```

Every spice's `test/suite` file now ends this way -- see
`docs/upcoming/spices/test-suites-report-success-with-failing-assertions.md` in the
turmeric repo, resolved.

## What this does not protect against

Stated plainly, because a security tool that overstates its scope is worse
than none:

- **Register and stack spills.** The C compiler owns register allocation.
  Copies of key material it made in registers or spill slots are not tracked
  and not wiped. This is the tier libsodium, Rust `zeroize`, and Zig
  `secureZero` occupy; Go's `runtime/secret` is above it because the Go
  runtime owns the stack. `secret-do` (phase S4) would be a heuristic scrub,
  not a fix.
- **Pointer escape from `with-secret`.** The borrow checker stops the
  `Secret` escaping, but cannot follow the raw `ptr<void>` handed to the
  callback. Stash that pointer and you have a dangling reference to freed key
  material.
- **Core dumps on macOS.** No `MADV_DONTDUMP` analog exists. Accepted gap.
- **The interpreter.** None of the memory guarantees hold under
  `tur --interpret`: turi runs inline-C definitions only via registered
  natives or a pattern-matching heuristic, and none of these bodies match;
  its values are process-lifetime by design, so there is no point at which
  "the secret is gone" can be promised. Write code that uses this spice in the
  REPL; do not hold real key material there.
- **Everything above the process.** Swap that was written before the `mlock`,
  a debugger attached to the process, a hypervisor, or a backup of the
  machine's RAM.

## Where it sits

| | erasure guarantee | static enforcement |
|---|---|---|
| Go `runtime/secret` | registers + goroutine stack, by the runtime | none |
| libsodium / `zeroize` | heap buffer, best-effort | none |
| Rust `zeroize` + `Drop` | heap buffer, best-effort | destructor runs, but forgetting to *use* the type is silent |
| **tur-secret** | heap buffer, best-effort | **forgetting to destroy is a compile error** |

The honest summary: runtime hygiene at the libsodium tier, with a static
guarantee none of the others express. Turmeric cannot make Go's promise, and
this spice does not claim to.
