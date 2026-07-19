# Stage-39 fixed-point kernel — MIND deterministic Cholesky solve (PoC)

This folder contributes a **deterministic fixed-point (Q16.16) Cholesky solve**
toward XRM-SSD's Phase-1 roadmap step:

> *Rust rewrite Stage 39 → fixed-point optimization → SP1/RISC Zero → replace `integrity_proof_stub.py`*

## Why this matters for the ZK phase

A ZK proof over Stage-39 Adaptive Nyström Attention is only *meaningful* if every
node computes the **bit-identical** result for the same input — otherwise honest
nodes produce different proofs and never reach consensus. Floating-point Stage 39
will not give you that across machines: reduction order, FMA contraction, and SIMD
reassociation drift between CPUs, and in an ill-conditioned solve that drift
amplifies into a *different* answer.

A **fixed-point deterministic kernel** removes the choice. In Q16.16, every product
is one integer multiply + one truncating `>> 16`, every division is `(a << 16) / b`,
and integer addition is exactly associative — so the whole factorization and solve
are bit-identical across x86 / ARM / GPU and across re-runs.

## What Nyström needs, and what this provides

Nyström reconstruction is `Ã = C · W⁺ · Cᵀ`, where `W⁺` is the pseudo-inverse of the
symmetric-PSD `m×m` landmark kernel `W`. The numerically-stable primitive for a
symmetric-PSD inverse-apply is a **Cholesky solve** (`A = L·Lᵀ`, then forward/back
substitution), not a general inverse. This file implements exactly that in Q16.16:

- `qsqrt_q16` — deterministic integer-Newton square root of a Q16.16 value (the one
  transcendental; exact on perfect squares).
- `cholesky_solve_q16(a, b, x, l, m)` — factor + forward/back substitution to solve
  `A · x = b` (= `W⁺ · b`). i64 accumulate, `(a*b) >> 16` multiply, `(a << 16) / b`
  divide. Fails loud (`-1`) on a non-positive pivot.

## Status: **m = 4 proof-of-concept**

This is a **de-risking prototype**, not the production kernel:

- **Proven correct:** solves a 4×4 SPD block and recovers `x = [1, 2, 3, 4]`
  exactly — matched independently by a Q16.16 reference model and the compiled ELF.
- **Proven deterministic:** the output encodes to one hash byte-stable across
  16 runs × 2 fresh builds; two independent builds are byte-identical. By the
  per-product-`>>16` / i64-accumulate associativity argument it is avx2 == neon by
  construction.

## Scale-up path (documented in-source)

Q16.16 has ~4.5 decimal digits of headroom, so at larger `m` the raw `a*b` and
`a << 16` are the overflow walls and near-singular pivots stress `qdiv`. The upgrade
is a **wider (i128) intermediate accumulator + diagonal pre-scale** — both
pure-integer, so bit-identity is preserved. Targeting the real landmark sizes
(256 / 512 / 1024 / 2048) is the next step and wants the actual `m`, tolerance
regime, and landmark-kernel shape from the XRM side.

## Build & run

Built and run on the shipped MIND compiler (`mindc`):

```
mindc contrib/mind-stage39/cholesky_q16.mind --emit-shared /tmp/cholesky_q16.so
mindc check contrib/mind-stage39/cholesky_q16.mind
```

Call the exported `cholesky_solve_q16` via the i64 C-ABI (opaque `int64` pointers).
