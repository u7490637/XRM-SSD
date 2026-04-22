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
PyO3 + Rust-cdylib overhead in `main.rs` and to lock the runtime
behind STARGA's `[protection]` transform set.

- **Run instructions for Polo:** [`POLO_INSTRUCTIONS.md`](POLO_INSTRUCTIONS.md)
- **Port source:** `examples/xrm_mind_port.mind`
- **Test harness:** `test-harness/` — `make check-env`, `run-hybrid-capture`,
  `parse-hybrid`, `seal`, `build`, `run`, `compare`
- **Protection manifest:** `protection/` — `Mind.toml`, `exports.map`, README

The harness does **not** fabricate numbers. `make compare` reads JSON
from both the existing hybrid run (parsed from Polo's unmodified `main.rs`
stdout) and the MIND-port run on the same host, then writes a
side-by-side table. Cells are marked `not measured` when a side hasn't
been run yet; no projected TPS values are shipped.

Running the MIND port requires `mindc` (STARGA internal toolchain) and
the XRM-SSD V23.3 reflection blob (Dollarchip). Running the hybrid
baseline requires only the existing `xrm_ssd_v23_3_integration/`
Rust+Python setup — no changes to `main.rs`.

See [`POLO_INSTRUCTIONS.md`](POLO_INSTRUCTIONS.md) for the step-by-step,
`test-harness/README.md` for harness internals, and
`protection/README.md` for what's locked in the MIND-side binary.
