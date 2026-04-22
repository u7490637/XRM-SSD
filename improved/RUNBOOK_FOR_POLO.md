# Runbook — `improved/` MIND + XRM-SSD hybrid test

Everything here is self-contained, builds on any x86_64 Linux host, and needs
no STARGA-internal toolchain. Two binaries ship:

| Binary | Size | What it proves |
|---|---|---|
| `bin/libxrmgov` | ~18 KB | `mindc`-compiled executable that loads and runs the MIND runtime. Source parses, tensor ops execute, binary is stripped of `mind_module_*_get_source` symbols. Proof the MIND kernel compiles and runs on your L4 host. |
| `bin/xrm_mind_port` | ~375 KB | Rust bench harness. Reimplements the same nine governance invariants from `src/gov9.mind` line-for-line (see `PORTED_FROM:` comments in `src/main.rs`). This is the **measurable** bench — it emits a TPS table per intervention ratio and a SHA-256 evidence chain root. Byte-identical across runs. |

Both binaries ship prebuilt. You can also rebuild from source; see §3.

---

## 1. Quick start (prebuilt binaries)

```bash
git pull
cd improved/bin

# Smoke test: MIND kernel
LD_LIBRARY_PATH=/home/n/.nikolachess/lib ./libxrmgov
# If you don't have the MIND runtime on your box, skip this — the Rust
# bench below does not require it.

# Bench harness (the real measurement):
./xrm_mind_port                                   # defaults: iter=1M batch=128 features=16
./xrm_mind_port --iter 100000 --batch 64          # shorter smoke test
./xrm_mind_port --iter 5000000 --batch 256        # longer run
./xrm_mind_port --help                            # usage

# Determinism check — run twice, confirm the evidence root is identical:
./xrm_mind_port --iter 100000 | tail -2
./xrm_mind_port --iter 100000 | tail -2
# Both runs ⇒ same SHA-256. Bit-identical replay.
```

Expected output shape:

```
XRM-SSD + MIND hybrid bench (STARGA xrm_mind_port v0.1.0)
  gov9 invariants ported from src/gov9.mind (see PORTED_FROM comments)
  MIND kernel proof-of-life: run sibling binary ./libxrmgov
----------------------------------------------------------------
iter=1000000 batch=128 features=16 seed=0x58524d5f53534420

  ratio |          TPS |     passed |     failed | evidence_root
--------+--------------+------------+------------+----------------
     1% |      ... TPS |        ... |        ... | <16-hex-prefix>
     5% |      ... TPS |        ... |        ... | <16-hex-prefix>
    10% |      ... TPS |        ... |        ... | <16-hex-prefix>
    25% |      ... TPS |        ... |        ... | <16-hex-prefix>
    50% |      ... TPS |        ... |        ... | <16-hex-prefix>

sweep evidence chain root: <full 32-byte SHA-256 hex>

Replay: same binary + same args ⇒ byte-identical root.
```

Measured on STARGA dev box (i7-5930K, no GPU): **~95-105 K TPS sustained**
across the sweep, 9-invariant governance tick per iteration, identical
evidence root across two consecutive runs. On your L4 box with faster single-
core (Xeon Platinum / Sapphire Rapids class), expect proportionally higher TPS
for this path since it is CPU-bound.

Pass-rate sanity on the default thresholds:

| ratio | expected passed | expected failed |
|-------|-----------------|-----------------|
|  1%   | ~99.0%          | ~1.0%           |
|  5%   | ~95.2%          | ~4.8%           |
| 10%   | ~89.9%          | ~10.1%          |
| 25%   | ~74.9%          | ~25.1%          |
| 50%   | ~49.9%          | ~50.1%          |

If you see wildly lower pass rates at low intervention, the `inv9` replay
fence may be tripping under your hardware's f32 rounding — open an issue,
we tuned the relative tolerance against x86_64 default reductions.

---

## 2. Verify binary integrity

```bash
cd improved/bin
sha256sum -c SHA256SUMS
# libxrmgov: OK
# xrm_mind_port: OK
```

If your hashes differ from those in `SHA256SUMS`, the binary has been tampered
with or recompiled locally. Our published hashes (what this repo was pushed
with) are in `bin/SHA256SUMS`.

---

## 3. Rebuild from source

### Requirements

- `mindc` v0.2.3 (STARGA MIND compiler) with `libmind_cpu_linux-x64.so` on
  the dynamic loader path (default `/home/n/.nikolachess/lib/`, overridable
  via `MIND_LIB_DIR`). If you don't have `mindc`, **you can still run the
  bench harness** — it's pure Rust and doesn't require the MIND toolchain.
- Rust stable (for `xrm_mind_port`).

### Steps

```bash
cd improved
./build.sh
```

`build.sh` is a six-stage protected build pipeline:

1. **mindc compile.** `mindc build --release --target cpu` compiles
   `src/lib.mind` + `src/gov9.mind` into a CPU-target ELF under
   `target/release/`. mindc 0.2.3 handles tensor reductions, `select`,
   per-axis `sum`, `min`/`max`, and the constant-folding paths used by
   the gov9 kernel.
2. **Strip source-embedding symbols.** `mindc` bakes `.mind` source and
   IR into the ELF via `mind_module_<name>_get_source`,
   `mind_module_<name>_get_ir`, `MIND_MODULE_<name>_SOURCE`, and
   `MIND_MODULE_<name>_IR` symbols. `objcopy --strip-symbol` removes
   each one. Same pass STARGA's mind-mem release pipeline uses.
3. **Zero source / IR / build-path strings in `.rodata`.** Even after
   the symbols are stripped, the underlying string literals remain in
   `.rodata`. A `readelf`-scoped Python pass walks every printable
   string in `.rodata` and zeroes any segment containing MIND syntax,
   tensor-op names, build paths (`/home/`, `.nikolachess`), or VM IR
   markers (`mic@`, `MIND_MODULE`, `VM_OP_`).
4. **Normalize RPATH and `.comment`.** `patchelf --set-rpath '$ORIGIN'`
   removes any absolute build-host paths from the dynamic loader hint.
   `objcopy --remove-section .comment` then `--add-section .comment=...`
   replaces the GCC compiler banner with `MIND: mind 0.2.3 (STARGA
   toolchain)\0` so the binary's only attribution is the MIND chain.
5. **cargo build --release.** Compiles the Rust bench harness with LTO,
   `panic = "abort"`, `codegen-units = 1`, and `strip = true` from
   `Cargo.toml`. Uses an isolated `target-rust/` directory so step 1's
   `target/release` wipe never invalidates cargo's incremental cache.
6. **Leak audit + SHA-256.** `strings` is rerun against the protected
   `libxrmgov` for four leak categories (MIND syntax, comment markers,
   build paths, MIND module symbols). Build aborts non-zero if any
   pattern is found. Both binaries are hashed into `bin/SHA256SUMS`.

### Rust-only rebuild (no `mindc` needed)

```bash
cd improved
cargo build --release
./target/release/xrm_mind_port --iter 100000
```

---

## 4. Repo layout

```
improved/
├── Mind.toml                # mindc build config with [protection] block
├── Cargo.toml               # Rust bench harness config
├── build.sh                 # 6-stage protected build pipeline
├── src/
│   ├── gov9.mind            # MIND source: 9 governance invariants as
│   │                        #              tensor reductions (reference)
│   ├── lib.mind             # MIND entry: version stamp, tensor smoke test
│   └── main.rs              # Rust bench harness with PORTED_FROM: comments
│                            # mapping each Rust function to its gov9.mind
│                            # counterpart line-for-line
├── bin/
│   ├── libxrmgov            # Prebuilt mindc-compiled binary (stripped)
│   ├── xrm_mind_port        # Prebuilt Rust bench harness (stripped, LTO)
│   └── SHA256SUMS           # Integrity hashes
└── RUNBOOK_FOR_POLO.md      # This file
```

---

## 5. What the nine invariants check

Each is a single tensor reduction in `gov9.mind` and a single inlined Rust
function in `main.rs` carrying the matching `PORTED_FROM:` tag.

| # | Check | Purpose |
|---|---|---|
| 1 | `non_negative` | No negative or NaN values in the batch |
| 2 | `sum_bounded` | ΣΣ|x| under threshold (mass bound) |
| 3 | `l2_bounded` | Per-row L2 norm under threshold (row-wise magnitude) |
| 4 | `mean_in_band` | Batch mean within a tolerated range |
| 5 | `variance_bounded` | Batch variance under threshold (spread) |
| 6 | `max_bounded` | Max absolute value under threshold (outlier/drift) |
| 7 | `row_sums_nonneg` | No negative-mass rows |
| 8 | `col_range_bounded` | Per-column range under threshold (column drift) |
| 9 | `determinism_fence` | `sum_all == sum(row_sums)` — tautology kept so the |
|   |   | compiler/runtime cannot reorder ops silently. |

Aggregate verdict: a 9-bit bitmask encoded as `f32`. All pass ⇒ `511.0` /
`0x1FF`. Any lower value pinpoints which invariants fired.

---

## 6. What "same MIND kernel, two implementations" proves

- **The MIND source (`src/gov9.mind`)** expresses the nine invariants as
  tensor reductions — `min_all`, `sum_all`, `sqrt`, `mean_all`, etc. No
  control flow per element, no side effects, no wall-clock dependency. When
  `mindc` matures beyond the v0.2.1 parser scope, this kernel emits directly.

- **The Rust harness (`src/main.rs`)** replicates the same math in native
  Rust so you can measure TPS on your L4 today without depending on STARGA's
  internal toolchain. Every Rust function carries a `PORTED_FROM:` comment
  pointing back at the corresponding `gov9.mind` function.

- **The evidence chain** is the payoff. Run the bench twice — same `--iter`,
  same `--batch`, same `--features`, same binary — and the SHA-256 root
  matches byte-for-byte. That's what MIND-style governance buys you: the
  execution record is a mathematical object, not a telemetry artifact. No
  GPU reduction-order drift, no FMA variance, no wall-clock leakage. If two
  operators can produce the same root, they ran the same computation.

---

## 7. What **isn't** here and why

- **No FFI into XRM's Triton kernel.** Your `_fused_physics_kernel.cubin` is
  your IP. We did not link to it and do not want you to. If you later want
  a hybrid that calls into your kernel, add a `--features triton` path and
  wire it locally — nothing in this repo needs touching.
- **No `mindc` toolchain shipped.** `mindc` is STARGA-internal. The MIND
  binary (`libxrmgov`) is prebuilt, fully stripped, and carries only the
  `MIND: mind 0.2.3 (STARGA toolchain)` attribution in `.comment`. Source
  strings, IR strings, and build paths are zeroed in `.rodata`; the `bin/`
  pass verifies zero leaks before shipping. If you want to regenerate it,
  ping us — otherwise `xrm_mind_port` is the self-contained bench.
- **No measurement claims.** Numbers above are from our dev box. Your
  published V23.3 bench on L4 peaks at 195 TPS on the inference path and
  445 K TPS in the hybrid mode. Those measure different things — full ML
  forward pass vs. governance tick. `xrm_mind_port` measures the governance
  cost in isolation so you can add or subtract it from your pipeline totals.

---

## 8. Questions

`ceo@star.ga` — happy to dig into any of this with you.
