# Vendored LMDB

`mdb.c`, `midl.c`, `lmdb.h`, `midl.h`, `LICENSE` and `COPYRIGHT` are copied
verbatim from LMDB's `libraries/liblmdb/`, unmodified.

| | |
| --- | --- |
| Upstream | https://github.com/LMDB/lmdb (mirror of git.openldap.org/openldap/openldap LMDB) |
| Tag | `LMDB_0.9.33` |
| Commit | `3a29a24777c82a0165de813ae696a5068b5add30` |
| Commit date | 2024-05-21 |
| Vendored | 2026-10-25 |
| License | OpenLDAP Public License 2.8 (`LICENSE`, `COPYRIGHT` -- both must ship) |

SHA-256 of the vendored files:

```
2fd45dcef13f94ecc6650da19f341af38cceb53b9d3b88e854f621cc319dc1b1  mdb.c
91110543f2e84c516188c1e3960b1f504e1c60a4766027a782349c000276ca93  midl.c
ea935082e5d5ea7ba685ee599c617f28353060710b0369d220a55f64bb07f1c2  lmdb.h
cef8236dc7dce9b5346d212d31e8330f9158bacabe270b14ab1e99745db118c6  midl.h
310fe25c858a9515fc8c8d7d1f24a67c9496f84a91e0a0e41ea9975b1371e569  LICENSE
3c87f63656b6a12fda225b6f798c547fe89e3404f499e890b445761eb310fd4f  COPYRIGHT
```

## Refreshing

```sh
git clone --depth 1 --branch LMDB_0.9.<N> https://github.com/LMDB/lmdb /tmp/lmdb
cp /tmp/lmdb/libraries/liblmdb/{mdb.c,midl.c,lmdb.h,midl.h,LICENSE,COPYRIGHT} \
   spices/lmdb/c/lmdb/
```

Then update the table and hashes above, and run the spice's tests on every
CI platform before landing. Stay on 0.9.33 until a later tag is confirmed to
build on macOS arm64: a 0.9.34 report mentions a compile error in `mdb.c`
there (lmdb-spice-plan section 3). Do not take LMDB's `master` branch -- it
is the unreleased 1.0 line (different file format, `ntdll` on Windows).

## The command-line tools

`mdb_stat`, `mdb_dump`, `mdb_load` and `mdb_copy` are not vendored. For
debugging, build them from the same checkout (`make -C
libraries/liblmdb mdb_stat mdb_dump`), or install a distribution's
`lmdb-utils` / Homebrew's `lmdb` -- any 0.9.x reads these files. Point them
at a single-file env with `-n` (MDB_NOSUBDIR): `mdb_stat -n app.mdb`.
