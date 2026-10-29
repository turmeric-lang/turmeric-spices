# tur-linalg-sparse

Sparse float matrices for Turmeric: CSR and CSC storage, sparse
matrix-vector products, ILU(0), and sparse conjugate gradient / BiCGSTAB.

## Overview

`tur-linalg-sparse` is a Tier 1 spice (pure Turmeric + inline-C only) built on
[`tur-linalg`](../linalg/): vectors are linalg's `lavec`, dense conversion goes
to and from linalg's `mat`, and the iterative solvers report through
`linalg/iter`'s `IterOpts` / `IterResult`. It adds no C dependencies.

Key features:
- `coomat` builder: push `(row, col, value)` triples in any order; duplicates
  are summed on compression
- `csrmat` / `cscmat` with binary-searched element access
- `csr-spmv` (A v) and `csc-spmv` (A' v), both O(nnz)
- `csr-scale`, `csr-add` (union of patterns), transposes and CSR <-> CSC
  re-storage, dense round trips
- `ilu0` incomplete LU with zero fill-in, and `ilu-apply`
- `sparse-cg` and `sparse-bicgstab`, which never densify A
- `csr->str` / `csr-print` as `(row, col) value` triples

## Install

```turmeric
:spices #map{"linalg-sparse" #map{:url    "https://github.com/turmeric-lang/turmeric-spices"
                                  :ref    "linalg-sparse-v0.1.0"
                                  :subdir "spices/linalg-sparse"}}
```

It depends on `tur-linalg` (v0.2.0 or later), which `tur fetch` resolves.

## Quick start

```turmeric
(import linalg/vec :refer [la-vec-of la-vec-free])
(import linalg/iter :refer [iter-opts iter-result-free])
(import linalg-sparse/csr :refer [coo-new coo-push! coo-free csr-from-coo csr-free])
(import linalg-sparse/solve :refer [sparse-cg])

(defn main [] : int
  (let [c (coo-new 3 3)]
    (coo-push! c 0 0 2.0)
    (coo-push! c 0 1 -1.0)
    (coo-push! c 1 0 -1.0)
    (coo-push! c 1 1 2.0)
    (coo-push! c 1 2 -1.0)
    (coo-push! c 2 1 -1.0)
    (coo-push! c 2 2 2.0)
    (let [a (csr-from-coo c)
          b (la-vec-of 1.0 0.0 1.0)
          r (sparse-cg a b (iter-opts 100 1e-12 false))]
      (println (.iters r))
      (iter-result-free r)
      (la-vec-free b)
      (csr-free a)
      (coo-free c)
      0)))
```

## Modules

| Module | Description |
|--------|-------------|
| `linalg-sparse/csr` | `coomat` builder, `csrmat`, access, `csr-spmv` |
| `linalg-sparse/csc` | `cscmat`, access, `csc-spmv` (A' v), CSR <-> CSC |
| `linalg-sparse/ops` | scale, add, transpose, dense conversion |
| `linalg-sparse/solve` | `ilu0`, `ilu-apply`, `sparse-cg`, `sparse-bicgstab` |
| `linalg-sparse/fmt` | printing as `(row, col) value` triples |

## See also

- [Guide](https://spices.turmeric-lang.com/docs/html/guides/linalg-guide.html#sparse-matrices)
- Source: <https://github.com/turmeric-lang/turmeric-spices/tree/main/spices/linalg-sparse>
