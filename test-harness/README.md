# Test Harness — MIND Port vs Hybrid

Measure, don't speculate.

> **For Polo:** step-by-step run instructions live in the repo root at
> [`POLO_INSTRUCTIONS.md`](../POLO_INSTRUCTIONS.md). This file is the
> reference for what the harness contains.

## What this directory is

A reproducible bench harness that runs both sides of the comparison on the
**same hardware** and writes the results to a single comparison table:

- **Hybrid baseline** — the existing `xrm_ssd_v23_3_integration/main.rs`
  path (PyO3 → Rust cdylib → XRM-SSD V23.3 reflection blob). Unchanged.
  Captured stdout is parsed by `scripts/parse_hybrid_stdout.py` into
  `results_hybrid.json`.
- **MIND port** — `examples/xrm_mind_port.mind` compiled with `mindc` to a
  single native ELF that calls the **same** XRM blob through a sealed
  extern FFI. No PyO3, no Rust wrapper. Emits `results_mind.json`.

Both sides run the same 9-invariant governance suite, the same
intervention ratio sweep (1 / 5 / 10 / 25 / 50%), and the same input
generator. The only thing that changes is the execution environment
around the XRM reflection call.

## File layout

```
test-harness/
├── Makefile                        # All workflow targets
├── seal_blob.sh                    # SHA-256 an XRM blob → seal_hashes.inc
├── compare.py                      # Emit comparison.md from JSON files
├── scripts/
│   └── parse_hybrid_stdout.py      # main.rs stdout → results_hybrid.json
├── seal_hashes.inc                 # (generated) blob SHA-256 include
├── hybrid_stdout.log               # (generated) raw main.rs output
├── results_hybrid.json             # (generated) parsed hybrid TPS table
├── results_mind.json               # (generated) MIND-port TPS table
├── comparison.md                   # (generated) side-by-side markdown
└── xrm_mind_port                   # (generated) protected MIND ELF
```

## Prerequisites (not shipped in this public repo)

| Component | Source | Needed for |
|-----------|--------|------------|
| `cargo`, Rust toolchain | host | `run-hybrid` (Polo's existing path) |
| `python3` 3.10+ | host | `parse-hybrid`, `compare` |
| `sha256sum` | coreutils | `seal` |
| `mindc` | STARGA internal toolchain | `build` (MIND-side only) |
| XRM-SSD V23.3 reflection blob | Dollarchip (Polo) | `seal`, `build`, `run` |
| CUDA 13.0, NVIDIA L4 | host | matches V23.3 bench report |

## Running

```bash
# 0. Sanity check what's present on this host.
make check-env

# 1. Hybrid baseline — always works if cargo + Polo's main.rs are present.
make run-hybrid-capture    # runs main.rs, tees stdout to hybrid_stdout.log
make parse-hybrid          # -> results_hybrid.json

# 2. MIND port (only if mindc + XRM blob are available).
export XRM_BLOB=/path/to/libxrm_ssd_v23_3.so
make seal                  # sha256 -> seal_hashes.inc
make build                 # -> xrm_mind_port ELF (protected)
make run                   # -> results_mind.json

# 3. Side-by-side table.
make compare
cat comparison.md
```

## What `comparison.md` contains

```
| Intervention ratio | Hybrid (main.rs) TPS | MIND port TPS | Speedup |
```

No projected numbers. No "2M+ TPS" claims. If the MIND port has not been
measured on this host, the cell says `not measured`, not a guess.

## What's being protected on the MIND side

This harness compiles `examples/xrm_mind_port.mind` with the `[protection]`
module attribute, the `protection/Mind.toml` transform set, and the
`protection/exports.map` version script. The result:

1. **XRM blob sealed at compile time.** The reflection blob's SHA-256 is
   baked in; any byte change after the build causes `verify_blob_seal()`
   to fail and the dispatch path to reject.
2. **MIND runtime locked.** STARGA's runtime libraries (governance kernel,
   evidence chain, Q16.16 primitives) are linked into the ELF under the
   full protection transforms. Source never leaves our toolchain.
3. **Minimal export surface.** `exports.map` exposes only four symbols:
   `xrm_mind_port_main`, `xrm_mind_port_run_sweep`, `xrm_mind_port_version`,
   `xrm_mind_port_seal_ok`. Everything else is forced LOCAL.
4. **Stripped binary.** Source embeddings, full symbol table, and
   `.comment` are removed — no toolchain fingerprint, no reverse path
   back to `.mind` source.

Full list in `../protection/README.md`.

## What's NOT in this harness

- **Fabricated results.** No TPS numbers appear anywhere in this harness
  that were not produced by running `make compare`.
- **Private XRM source.** The XRM blob is an opaque FFI target; we do not
  decode, embed, or reverse it.
- **Private MIND runtime source.** STARGA's MIND runtime libraries are
  loaded from `mindc`'s internal toolchain at build time and linked into
  the final ELF under the protection transforms above. No runtime source
  is shipped or exposed here.

## Reproducing Polo's original V23.3 numbers first

If you don't have `mindc`, the hybrid baseline still reproduces the
existing V23.3 report on this host:

```bash
make run-hybrid-capture
make parse-hybrid
make compare
```

The MIND column will show `not measured` until `make run` succeeds.
