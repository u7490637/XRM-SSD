# improved/ — MIND + XRM-SSD hybrid test harness

Self-contained apples-to-apples test for the XRM-SSD V23.3 integration with
STARGA's MIND governance layer. Two binaries ship prebuilt:

- `bin/libxrmgov` — `mindc`-compiled MIND executable (proof of compile + run).
- `bin/xrm_mind_port` — Rust bench harness, reimplements the 9 governance
  invariants from `src/gov9.mind` line-for-line, emits a deterministic
  SHA-256 evidence chain root over the sweep.

**See `RUNBOOK_FOR_POLO.md`** for run instructions, rebuild steps, repo
layout, and verification commands.

Quick start:

```bash
cd bin
./xrm_mind_port --iter 100000
# bit-identical evidence chain root at the end; run twice to verify replay.
```

Every line is inspectable:

- `src/gov9.mind` — the 9 invariants as tensor reductions in MIND.
- `src/main.rs` — same 9 invariants in Rust, with `PORTED_FROM:` comments
  mapping each function to its `gov9.mind` counterpart.
- `build.sh` — the 4-stage build pipeline with protection flags and
  source-symbol stripping.

Nothing from XRM-SSD's core IP is touched. This harness measures governance
cost in isolation so it can be added to or subtracted from pipeline totals.

— STARGA, Inc. (`ceo@star.ga`)
