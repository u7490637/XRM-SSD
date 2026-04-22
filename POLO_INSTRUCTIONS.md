# For Polo — How to Run the MIND Port vs Hybrid Bench

One-page runbook. Everything below runs on your existing Lightning AI
L4 host. No Python rewrite. No changes to your `main.rs`.

---

## What this produces

A single `comparison.md` with measured TPS for both sides on the
**same hardware**:

```
| Intervention ratio | Hybrid (main.rs) TPS | MIND port TPS | Speedup |
| 1%                 | …                    | …             | …       |
| 5%                 | …                    | …             | …       |
| 10%                | …                    | …             | …       |
| 25%                | …                    | …             | …       |
| 50%                | …                    | …             | …       |
```

No projected numbers — cells say `not measured` when a side has not
been run yet. When both sides are measured, the speedup fills in
automatically.

See also `CAPABILITIES.md` for what the MIND port delivers beyond TPS
(sealed blob, deterministic replay, evidence chain, attestation).

---

## Prereqs on your Lightning AI L4

Run this first — it is diagnostic, never fails the build:

```bash
cd test-harness
make check-env
```

It prints a table of what is present:

| Tool | Needed for | Who provides it |
|---|---|---|
| `cargo` + Rust | hybrid baseline | you (already used) |
| `python3` 3.10+ | parse + compare | host |
| `sha256sum` | seal | coreutils |
| `mindc` | MIND build | **STARGA internal** — ask us |
| XRM V23.3 reflection blob | seal + build + run | **you** — set `XRM_BLOB=…` |
| NVIDIA L4 + CUDA 13 | both sides | host |

---

## Path A — just the hybrid baseline (no MIND needed)

This reproduces your existing V23.3 result on this host and parses it
into the JSON the comparison uses. You can do this today.

```bash
git clone https://github.com/u7490637/XRM-SSD.git
cd XRM-SSD
git checkout mind-port-offscale
cd test-harness

make run-hybrid-capture   # runs your main.rs, tees stdout to hybrid_stdout.log
make parse-hybrid         # -> results_hybrid.json
make compare              # -> comparison.md (MIND column: not measured)
cat comparison.md
```

If this is all you do, we will use `results_hybrid.json` as the
apples-to-apples baseline and run the MIND side on our hardware.

---

## Path B — full comparison on your L4

Two things you need:

1. The XRM reflection blob path on this host, e.g. `libxrm_ssd_v23_3.so`
2. `mindc` — STARGA's MIND compiler (we ship the compiled binary to you
   on request; the compiler itself does not need to live on your host
   if we do the build and hand you the ELF)

### B1 — you have `mindc` on the L4

```bash
cd test-harness
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so

make check-env         # sanity
make seal              # sha256 -> seal_hashes.inc (compile-time bake)
make build             # -> ./xrm_mind_port ELF (fully protected)
make run               # -> results_mind.json
make verify-replay     # -> runs the sweep twice, asserts chain roots match
make compare           # -> comparison.md
cat comparison.md
```

### B2 — we build the ELF on our side and ship it to you

```bash
# We send: xrm_mind_port-<githash>-l4-cuda.elf  and  seal_hashes.inc
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so

./xrm_mind_port --iterations 1000000 \
                --ratios 100,500,1000,2500,5000 \
                --json results_mind.json
./xrm_mind_port --replay --iterations 100000 \
                --json results_replay.json

python3 compare.py --mind results_mind.json \
                   --hybrid results_hybrid.json \
                   --out comparison.md
cat comparison.md
```

The ELF's seal refuses to run if `XRM_BLOB` has been byte-modified
since we sealed it. Rebuild the seal header if the blob changes.

---

## What the MIND port actually shows

Beyond a TPS delta, the MIND port produces four structural outputs
the hybrid path cannot produce (see `CAPABILITIES.md` for mechanisms):

1. **Sealed-blob verdict.** Each dispatch re-checks the XRM blob's
   SHA-256 before calling into it. If the blob on disk changes,
   `Verdict::Rejected(0)` fires and the bench exits non-zero.
2. **Evidence chain root per ratio.** Appears in `results_mind.json`
   as `chain_root`. Two runs on two different substrates with the
   same input produce the same root, byte for byte.
3. **`make verify-replay`.** Runs the sweep twice and asserts all five
   chain roots are equal. Exit 0 = deterministic; exit non-zero =
   something (usually a hardware-side non-deterministic path) broke.
4. **Per-ratio attestation.** Pairs the logical chain depth with a
   physical-clock skew fingerprint of your L4 host. Lets a third
   party verify the binary ran on the hardware it was sealed for.

---

## If something goes wrong

- `make check-env` shows what is missing on this host.
- `make run-hybrid-capture` fails → check `hybrid_stdout.log`. It is
  your existing `main.rs`; we do not modify it.
- `make build` fails with `mindc: not found` → you do not have the
  STARGA toolchain. Switch to path B2 and we ship the ELF.
- `make build` fails with `XRM_BLOB not found` →
  `export XRM_BLOB=/absolute/path/to/libxrm_ssd_v23_3.so`.
- `./xrm_mind_port` exits non-zero immediately → the blob on disk no
  longer matches the SHA-256 that was sealed. Run `make seal` and
  `make build` again, then `make run`.
- `make verify-replay` fails (exit non-zero) → please send us the
  two `results_replay_a.json` and `results_replay_b.json`. The
  divergence is usually on your XRM side and we will help diff it.

---

## What we need from you to move faster

1. Path to `libxrm_ssd_v23_3.so` (or the `.cubin`) on this host.
2. Confirmation of the FFI signature. The MIND port currently assumes:

   ```c
   Hash256 xrm_reflect(
       const uint8_t *blob_ptr,
       const uint8_t *txn_bytes,
       uint64_t       txn_len);
   ```

   If yours is different, tell us what it is and we will adjust the
   `extern fn` in `examples/xrm_mind_port.mind`. No harness change.
3. Preference for B1 (mindc on your L4) vs B2 (we ship the ELF).

Reply on the thread with any of these and we will move immediately.

— Nikolai, STARGA
ceo@star.ga
