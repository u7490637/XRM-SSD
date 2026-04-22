# XRM-SSD V23.3 Public Performance Test Version

**Purpose**: This repository is solely for publicly demonstrating the actual performance test results of XRM-SSD V23.3 on a Lightning AI L4 GPU combined with TensorRT-LLM.

- This is a **test-only version** and does not contain the complete core code.

- The complete system is currently **patent-free**.

- You are welcome to view the test results and notebooks, but please do not use them for commercial purposes or make unauthorized modifications.

Test Environment:

- Platform: Lightning AI

- GPU: NVIDIA L4

- Backend: TensorRT-LLM

- Version: XRM-SSD V23.3

Test results will be regularly updated in the notebooks and benchmarks folders of this repository.

---

## MIND Port (branch: `mind-port-offscale`)

STARGA is porting the hybrid harness to native MIND to remove the
PyO3 + Rust-cdylib overhead in `main.rs` and to add four structural
properties the hybrid path cannot produce: sealed opaque FFI to the
XRM blob, deterministic replay, evidence-chain emission, and
substrate-bound attestation. Full capability map:
[`CAPABILITIES.md`](CAPABILITIES.md).

- **Run instructions for Polo:** [`POLO_INSTRUCTIONS.md`](POLO_INSTRUCTIONS.md)
- **Capability map (MIND vs hybrid):** [`CAPABILITIES.md`](CAPABILITIES.md)
- **Port source:** [`examples/xrm_mind_port.mind`](examples/xrm_mind_port.mind)
- **Test harness:** `test-harness/` — `make check-env`, `run-hybrid-capture`,
  `parse-hybrid`, `seal`, `build`, `run`, `verify-replay`, `compare`
- **Protection manifest:** `protection/` — `Mind.toml`, `exports.map`, `README.md`

The harness does **not** fabricate numbers. `make compare` reads JSON
from both the existing hybrid run (parsed from Polo's unmodified
`main.rs` stdout) and the MIND-port run on the same host, then writes a
side-by-side table. Cells are marked `not measured` when a side hasn't
been run yet. `make verify-replay` additionally runs the MIND sweep
twice and asserts every per-ratio chain root matches byte-for-byte —
the structural guarantee the hybrid path cannot make.

Running the MIND port requires `mindc` (STARGA internal toolchain) and
the XRM-SSD V23.3 reflection blob (Dollarchip). Running the hybrid
baseline requires only the existing `xrm_ssd_v23_3_integration/`
Rust+Python setup — no changes to `main.rs`.
