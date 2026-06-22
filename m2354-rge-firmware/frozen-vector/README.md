# Frozen Vector — Cross-Substrate Bit-Identity Target

One canonical input, one canonical output. Every implementation of the RGE
`rge_blend2_env` kernel — C host (`bridge.c`), Python (`rge_host.py`), Swift
(iPhone ARM64 Mirror Core), M2354 firmware (`rge.c`), and the MIND contract
(`lpcc2_*.mind`) — must fold **this exact vector** to **this exact integer**.

That is the real determinism proof. Every earlier "bit-identical" pass was a
single surface agreeing with *itself* on its *own* input (49001, 20971, …).
This is the first apples-to-apples target shared across all surfaces.

## The vector (Q16.16)

| field | value | ≈ |
|-------|-------|---|
| entropy | 45000 | 0.687 |
| kv | 32768 | 0.500 |
| g | 50000 | 0.763 |
| p | 28000 | 0.427 |
| r | 60000 | 0.916 |
| availability | 55000 | 0.839 |
| trust | 48000 | 0.732 |

## Expected output

| path | Q16 raw | float |
|------|---------|-------|
| normal | **26211** | 0.3999 |
| degraded | **25658** | 0.3915 |

Ground-truthed in C, byte-identical at `-O0` and `-O3 -march=native`.

## Verify (C)

```bash
gcc -O0 frozen_vector_gt.c -o fgt && ./fgt
# FROZEN normal   Q16=26211  (0.3999)
# FROZEN degraded Q16=25658  (0.3915)
```

The kernel in `frozen_vector_gt.c` is the exact single-divide scale, round-half
(`+0x8000 >> 16`) on every term, with locked accumulation order — identical to
`m2354_firmware/bridge.c`.

## What's left to close the four-way proof

1. **Confirm each surface hits 26211 on THIS vector.** Run C / Python / Swift /
   firmware on these 7 integers — all must print 26211 (normal).
2. **Add a Node-B vector.** This is single-node. The product is the *decision*
   (argmax over A and B), so a second frozen vector is needed to prove
   arbitration, not just one fold.
3. **Machine-verify the `.mind` corner.** Fold this vector through the MIND
   tensor surface on a `--features cpu-exec` build and confirm it prints 26211.
   That is the fourth corner — MIND in the proof, which is the actual wedge.
