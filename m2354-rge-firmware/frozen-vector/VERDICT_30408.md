# Frozen-vector ground truth vs the 30408 / 44523 claim

**Canonical target (re-verified locally 2026-06-22):**

| path     | Q16   | value  |
|----------|-------|--------|
| normal   | 26211 | 0.3999 |
| degraded | 25658 | 0.3915 |

Compiled `frozen_vector_gt.c` (the exact single-divide cJSON-version
`rge_blend2_env`) at `-O0` and `-O3 -march=native` — byte-identical
output, run-to-run stable. This is the authoritative number every
surface (C / Python / Swift / .mind) must hit on the frozen vector
(E=45000, KV=32768, G=50000, P=28000, R=60000, AV=55000, TR=48000).

## Why the reported 30408 / 44523 does not hold

1. **Degraded > normal is arithmetically impossible.** The degraded
   path only ever *down-weights* R (`w_r *= 0.9`). Removing weight from
   a positive term cannot raise the score. Ground truth has
   `degraded (25658) < normal (26211)`. The reported pair has
   `degraded (44523) > normal (30408)` — that ordering cannot occur for
   this kernel under any input. It means the degraded path is not
   down-weighting at all (a different formula, or the `/d/d`-class bug
   resurfacing).

2. **30408 / 44523 match nothing in the thread** — not this C ground
   truth (26211/25658), not bridge.c (49001/48280), not the iPhone PoC
   (20971). No derivation shown.

3. **The "`--features cpu-exec` auto-backfilled" claim is
   self-contradicting.** If the toolchain genuinely folded the frozen
   vector to 30408, then the `.mind` kernel disagrees with the C kernel
   (26211) — i.e. there is *no* bit-identity. You cannot claim both
   "machine-verified by cpu-exec" and "bit-identical across
   MIND/C/Python/Swift" when the backfilled number is not the C number.

## Ask

Post the **raw stdout of the fold**, not the header text. Two outcomes,
both useful:

- prints **26211** → the header was hand-edited again (now dressed as
  compiler output);
- prints **30408** → genuine MIND-vs-C divergence and the wedge is
  broken — that is the real bug to chase.

Canonical target stands at **26211 / 25658**.
