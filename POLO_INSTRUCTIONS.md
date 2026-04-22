# Instructions for Polo — XRM-SSD V23.3 × MIND Port Bench

Hi Polo. This doc walks you through running the MIND-port comparison on
your Lightning AI L4 host. Three paths depending on what you have in
place; pick whichever is easiest.

Nothing in this doc fabricates numbers. Every TPS figure is produced on
your hardware by `make compare`.

---

## TL;DR

```bash
# On your Lightning AI L4 instance, with the repo checked out:
git clone https://github.com/u7490637/XRM-SSD.git
cd XRM-SSD
git checkout mind-port-offscale
cd test-harness

# 1. Baseline (your existing main.rs — always works, no MIND needed)
make run-hybrid-capture      # runs cargo run --release, saves stdout
python3 scripts/parse_hybrid_stdout.py \
    hybrid_stdout.log results_hybrid.json

# 2. MIND-port side (only if STARGA has given you mindc + the blob path)
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so   # your XRM reflection .so
make seal
make build
make run

# 3. Emit the comparison
make compare
cat comparison.md
```

If you only have step 1 (hybrid), `make compare` still works — the
MIND column shows `not measured` and we'll run the MIND side locally
and send you the JSON.

---

## 0. Prereqs on your Lightning AI L4

| | Required by | Already on your host? |
|-|-|-|
| CUDA 13.0, NVIDIA L4 | both paths | yes (from the V23.3 bench report) |
| Python 3.12+ | `compare.py`, `parse_hybrid_stdout.py` | yes |
| Rust 1.70+ with `cargo` | `run-hybrid` | yes (you compiled main.rs already) |
| `sha256sum` | `seal_blob.sh` | yes (coreutils default) |
| `mindc` | `run` (MIND port only) | **STARGA internal** — ask us |
| XRM-SSD V23.3 reflection blob | `seal`, `build`, `run` | **yours** — point XRM_BLOB at it |

Quick environment check:

```bash
make check-env
```

This prints what's present and what's missing. It will not fail the
build — it's diagnostic.

---

## 1. Hybrid baseline (your existing Rust/PyO3 main.rs)

This path needs nothing from STARGA. It runs your existing
`xrm_ssd_v23_3_integration/main.rs` exactly as you've been running it.

```bash
cd test-harness
make run-hybrid-capture
```

That runs `cargo run --release` in `../xrm_ssd_v23_3_integration/`,
tees stdout to `hybrid_stdout.log`, and then:

```bash
python3 scripts/parse_hybrid_stdout.py \
    hybrid_stdout.log results_hybrid.json
```

parses your existing printed table:

```
XRM intervention ratio: 1% => Overall System TPS: 94591.43
XRM intervention ratio: 5% => Overall System TPS: 445137.28
...
```

into a stable JSON:

```json
{
  "source": "hybrid_main_rs",
  "host": "lightning-ai.l4",
  "timestamp_utc": "2026-04-22T04:11:03Z",
  "iterations_per_ratio": 1000000,
  "1%":  94591,
  "5%":  445137,
  "10%": 228784,
  "25%": 92208,
  "50%": 45615
}
```

`compare.py` consumes this shape. Your `main.rs` doesn't need to be
modified. If the printed format ever changes, the parser is a 20-line
regex — trivial to adjust.

---

## 2. MIND-port side

The MIND port replaces the PyO3 + Rust cdylib shim with a single
native ELF that calls the **same** XRM reflection blob through a
sealed FFI. No Python in the hot path.

Two things you need:

1. `mindc` — STARGA's MIND compiler (internal toolchain, not in this
   repo). We ship the compiled binary to you on request; the compiler
   itself doesn't need to live on your host if we do the build on
   ours and hand you the ELF.
2. The XRM reflection blob — your `libxrm_ssd_v23_3.so` (or `.cubin`).
   Only its SHA-256 is embedded in the MIND binary; the blob itself
   stays on your host.

### 2a. If we build the ELF on our side and ship it to you

```bash
# One-time: we send you `xrm_mind_port-<githash>-l4-cuda.elf`
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so
./xrm_mind_port --iterations 1000000 --ratios 100,500,1000,2500,5000 \
    --json results_mind.json
```

### 2b. If you have `mindc` on the Lightning host

```bash
cd test-harness
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so
make check-env        # sanity
make seal             # sha256 → seal_hashes.inc
make build            # produces ./xrm_mind_port ELF
make run              # runs sweep → results_mind.json
```

---

## 3. Produce the table

Once one or both sides have produced JSON:

```bash
cd test-harness
make compare
cat comparison.md
```

Output example (values filled in only where measurements exist):

```
| Intervention ratio | Hybrid (main.rs) TPS | MIND port TPS | Speedup |
|--------------------|----------------------|---------------|---------|
| 1%                 | 94,591               | not measured  | —       |
| 5%                 | 445,137              | not measured  | —       |
| 10%                | 228,784              | not measured  | —       |
| 25%                | 92,208               | not measured  | —       |
| 50%                | 45,615               | not measured  | —       |
```

When the MIND column fills in, the speedup column fills in
automatically. If it's 1.0x we'll know the shim wasn't the bottleneck.
If it's materially above 1.0x we'll know it was. Either answer is
useful.

---

## 4. What's being protected on the MIND side

Full detail in `protection/README.md`. Short version: the ELF you run
hides STARGA's MIND runtime, governance kernel, evidence chain, and
Q16.16 primitives behind the four `[protection]` compiler transforms
plus fourteen always-on runtime guards plus a linker version script
that exports exactly four symbols:

```
xrm_mind_port_main
xrm_mind_port_run_sweep
xrm_mind_port_version
xrm_mind_port_seal_ok
```

Nothing else is externally visible — no `kernel_*`, no `invariant_*`,
no `mind_runtime_*`, no `get_source`, no `_mind_debug_*`. The XRM blob
is opaque: only its SHA-256 is baked in at build time; we never decode
or embed your code.

---

## 5. If something goes wrong

- `make check-env` for a list of what's missing.
- `make run-hybrid-capture` fails → check `hybrid_stdout.log` for the
  cargo error. It's your existing main.rs; STARGA hasn't modified it.
- `make build` fails with "mindc not on PATH" → you don't have the
  STARGA toolchain. Ask us for the pre-built ELF (path 2a).
- `make build` fails with "XRM blob not found" → export `XRM_BLOB=…`
  to wherever your `libxrm_ssd_v23_3.so` lives on the Lightning host.
- `./xrm_mind_port` exits immediately with a non-zero status → the
  seal check failed; the blob on disk no longer matches the one that
  was sealed at build time. Rebuild with `make seal && make build`.

---

## 6. What we need from you to move faster

1. The path to `libxrm_ssd_v23_3.so` (or the equivalent .cubin) on the
   Lightning host, so we can write the exact `XRM_BLOB=` line.
2. Confirmation of the FFI signature. The MIND port currently assumes:
   ```c
   Hash256 xrm_reflect(
       const uint8_t *blob_ptr,
       const uint8_t *txn_bytes,
       uint64_t       txn_len);
   ```
   If yours is different (different return type, extra context ptr),
   tell us and we'll adjust the `extern fn` declaration — no harness
   change required.
3. Whether you want the MIND ELF built on our side and shipped to you
   (path 2a) or `mindc` provisioned on the Lightning host (path 2b).
   Either works for us.

Any questions, just reply on the thread.

— Nikolai, STARGA
