# MIND Capabilities on the XRM-SSD V23.3 Bench

This document enumerates what the MIND port does that the existing
PyO3 + Rust-cdylib hybrid (`xrm_ssd_v23_3_integration/main.rs`) cannot.
It is a capability map, not a marketing sheet: every row names a
property the hybrid path lacks and a specific mechanism in
`examples/xrm_mind_port.mind` that supplies it.

The comparison table is produced by `make compare` on Polo's L4 host.
No TPS numbers appear in this document — only structural properties
that are either present or absent, verifiable by inspection.

## Side-by-side capability map

| Capability | Hybrid (main.rs) | MIND port | Mechanism in `xrm_mind_port.mind` |
|---|---|---|---|
| XRM reflection blob sealed at compile time | No | Yes | `SealedBlob.expected_hash` baked from `seal_hashes.inc` |
| Blob seal re-verified per dispatch | No | Yes | `verify_blob_seal()` in `dispatch()` |
| 9-invariant governance on every txn | Yes (Rust) | Yes (MIND) | `gov9_index_of_failure()` → `kernel.invariant_*` |
| Actionable reject reason (which invariant failed) | No | Yes | `Verdict::Rejected(u32)` returns failing index 1..9 |
| Q16.16 fixed-point inputs only | Mixed (IEEE 754 in Python) | Yes | `q16_weights: [Q16_16; 16]` |
| Cross-target bit-identical math | Not guaranteed | Yes (by type system) | Q16.16 eliminates FMA / reduction-order variance |
| Evidence-chain emission per accept | No | Yes | `evidence_root_for()` — Merkle append |
| Chain root returned with every verdict | No | Yes | `Verdict::MindAccept(root)`, `Verdict::XrmReflected(root, _)` |
| Deterministic replay (same input → same chain) | Not verifiable | Yes | `replay_verify()` runs sweep twice, asserts equal roots |
| Physical-clock skew fingerprint | No | Yes | `clock::physical::fingerprint_local()` |
| Substrate-bound attestation of the run | No | Yes | `attest_run()` seals (logical_tick, physical_skew) |
| Runtime source shipped to customer | Via shim | No | `[protection]` + `exports.map` forces everything LOCAL |
| External symbol surface | Rust-level (ABI-leaky) | Five symbols | `protection/exports.map` |
| Anti-debug / anti-tamper in final binary | No | Yes | `anti_debug` + `anti_tamper` transforms in `Mind.toml` |
| `.comment` section cleared of toolchain fingerprint | No | Yes | `[protection.linker].strip_comment = true` |

Every row is grep-checkable in the corresponding file — we're not asking
for trust, we're naming the mechanism.

## The four capabilities the hybrid path structurally cannot add

These are not performance deltas — they are properties the hybrid
architecture cannot acquire without being rewritten.

### 1. Sealed opaque FFI into the XRM blob

`main.rs` links XRM as a dynamic library through Rust's normal loader.
Any process with `LD_PRELOAD` or a swapped `.so` changes XRM's behavior
silently; the shim has no integrity stake in the blob.

The MIND port bakes the blob's SHA-256 into the binary at build time
and re-verifies it before every `xrm_reflect()` call. A single byte
change to `libxrm_ssd_v23_3.so` after the build causes `verify_blob_seal()`
to return `false` and `dispatch()` to emit `Verdict::Rejected(0)`.

Mechanism: `SealedBlob`, `verify_blob_seal()`, `test-harness/seal_blob.sh`.

### 2. Deterministic replay as a compile-time guarantee

IEEE 754 in the Python layer plus GPU reduction-order variance in the
XRM path makes the hybrid output approximately-reproducible at best.
"Run the same input twice and get the same answer" is a best-effort
runtime property.

The MIND port inputs are `[Q16_16; 16]` — exact integers — and the
accept path emits a chain root through `evidence_chain.append`. Two
runs on two different substrates with the same input produce
byte-identical chain roots. `replay_verify()` ships this as a test.

Mechanism: `TxnInput.q16_weights`, `evidence_root_for()`, `replay_verify()`.

### 3. Evidence chain as a first-class output

The hybrid prints a TPS number. It does not retain per-decision
provenance, it does not emit a Merkle root, it does not allow a
third party to independently verify that any specific decision was
reached through the stated governance path.

The MIND port returns a `Hash256` chain root with every accept.
Any auditor, regulator, or counterparty holding the sealed binary
and the chain root can re-run the input and verify the result byte
for byte. Admissible evidence, not telemetry.

Mechanism: `Verdict::MindAccept(root)`, `Verdict::XrmReflected(root, _)`,
`BenchResult.chain_root`.

### 4. Substrate-bound attestation (Page-Wootters clock pair)

The hybrid has one clock — wall time. It has no notion of whether
the binary is running on the hardware it was sealed for.

The MIND port exposes a three-fold clock: the logical clock (evidence
chain depth), the physical clock (RFC 1323 / CLOCK_MONOTONIC_RAW skew
fingerprint, ~30–100 ppm per-device spread), and an Ed25519 attestation
binding the two. A sealed binary running on a swapped-in host emits a
skew signature that does not match the baseline; the attestation is
the tamper signal.

Mechanism: `clock::LogicalTick`, `clock::PhysicalSkew`,
`clock::Attestation`, `attest_run()`.

## The protection surface

Separate from the capability additions above, the MIND port ships
under the full `[protection]` transform set and a linker version
script that exposes exactly four symbols on the final ELF:

```
xrm_mind_port_main
xrm_mind_port_run_sweep
xrm_mind_port_version
xrm_mind_port_seal_ok
```

Everything else — `kernel_*`, `invariant_*`, `gov9_*`,
`verify_blob_seal*`, `xrm_reflect*`, `dispatch*`, `evidence_chain_*`,
`clock_*`, `Q16_16_*`, `Hash256_*`, `mind_runtime_*`, `mind_rt_*`,
`mindc_*`, `_mind_source_*`, `_mind_debug_*`, `get_source*` — is
forced LOCAL. See `protection/exports.map` and `protection/README.md`.

## What this document does not claim

- No TPS numbers. Those come from `make compare` on the L4 host.
- No claim that the MIND port is "faster" until `results_mind.json`
  and `results_hybrid.json` both exist on the same host.
- No claim that any governance or attestation property is present
  in Polo's XRM-SSD core — we only describe what the MIND wrapper adds.
