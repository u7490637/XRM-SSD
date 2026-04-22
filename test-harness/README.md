# Test Harness — MIND Port vs Hybrid

Measure, don't speculate.

## What this directory is

A reproducible bench harness that runs both sides of the comparison on the
**same hardware** and writes the results to a single comparison table:

- **Hybrid baseline** — the existing `xrm_ssd_v23_3_integration/main.rs` path
  (PyO3 → Rust cdylib → XRM-SSD V23.3 reflection blob).
- **MIND port** — `examples/xrm_mind_port.mind` compiled with `mindc` to a
  single native ELF that calls the **same** XRM blob through a sealed extern
  FFI, no PyO3 and no Rust wrapper.

Both sides run the same 9-invariant governance suite, the same intervention
ratio sweep (1/5/10/25/50%), and the same input generator. The only thing
that changes is the execution environment around the XRM reflection call.

## Prerequisites (not shipped in this public repo)

| Component | Source | Notes |
|-----------|--------|-------|
| `mindc`   | STARGA internal toolchain | Required for `[protection]` builds |
| XRM-SSD V23.3 reflection blob | Dollarchip (Polo) | Place at `../libs/xrm_ssd_v23_3.so` or export `XRM_BLOB=…` |
| CUDA 13.0, NVIDIA L4 | host | Matches the original XRM-SSD V23.3 report |
| Python 3.12+ | host | Only for `compare.py` (pure stdlib) |

## Running

```bash
# 1. Seal the XRM blob (emits seal_hashes.inc with SHA-256)
make seal

# 2. Build the locked, protected MIND binary
make build

# 3. Run each side. Both emit a JSON file for compare.py to consume.
make run-hybrid      # writes results_hybrid.json
make run             # writes results_mind.json

# 4. Produce the side-by-side table
make compare
cat comparison.md
```

## What the table contains

```
| Intervention ratio | Hybrid (main.rs) TPS | MIND port TPS | Speedup |
```

No projected numbers. No "2M+ TPS" claims. If the MIND port has not been
measured on this host, the cell says `not measured`, not a guess.

## What's being protected on the MIND side

This harness compiles `xrm_mind_port.mind` with the `[protection]` module
attribute, the `protection/Mind.toml` transform set, and the
`protection/exports.map` version script. The result:

1. The XRM reflection blob is sealed by SHA-256 at compile time; any byte
   change after the build causes `verify_blob_seal()` to fail and the
   dispatch path to reject.
2. STARGA's MIND runtime inside the ELF exports only the explicit symbol
   set listed in `exports.map`. No governance internals, no `get_source`,
   no debug glue are externally visible.
3. The binary is stripped of source embedding, symbol names, and
   `.comment` metadata — no toolchain fingerprint, no reverse path back
   to `.mind` source.

Full list of runtime protections and compiler transforms that ship on
the MIND side: `protection/README.md`.

## What's NOT in this harness

- **Fabricated results.** No TPS numbers appear anywhere in this harness
  that were not produced by running `make compare`.
- **Private XRM source.** The XRM blob is an opaque FFI target; we do not
  decode, embed, or reverse it.
- **Private MIND runtime source.** STARGA's MIND runtime libraries are
  loaded from `mindc`'s internal toolchain at build time and linked into
  the final ELF under the protection transforms above. No runtime source
  is shipped or exposed.

## Reproducing the original XRM-SSD V23.3 numbers first

If you don't want to build the MIND port, you can still reproduce Polo's
existing hybrid benchmark:

```bash
make run-hybrid
```

That runs the unmodified `xrm_ssd_v23_3_integration/main.rs` and writes
its sweep into `results_hybrid.json`. The MIND port comparison is an
optional second step that requires `mindc`.
