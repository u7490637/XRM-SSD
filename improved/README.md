<p align="center">
  <img src="docs/img/mind-logo.svg" alt="MIND" width="180">
</p>

# improved/ — MIND + XRM-SSD hybrid test harness

Self-contained apples-to-apples test for the XRM-SSD V23.3 integration with
STARGA's MIND governance layer. Three artifacts ship prebuilt in `bin/`:

| File | Built by | Purpose |
|---|---|---|
| `libxrmgov` | `mindc` 0.2.3 (`src/lib.mind` + `src/gov9.mind`) | Proof MIND source compiles and runs. |
| `libmind_cpu_linux-x64.so` | STARGA MIND runtime | Linked next to `libxrmgov` via `RPATH=$ORIGIN`. |
| `xrm_mind_port` | `cargo` (LTO release) | Rust bench harness; reimplements the 9 invariants from `src/gov9.mind` line-for-line and emits a deterministic SHA-256 evidence chain. |

**See `RUNBOOK_FOR_POLO.md`** for run instructions, rebuild steps, repo
layout, and verification commands.

Quick start:

```bash
cd bin
./xrm_mind_port --iter 100000
# bit-identical evidence chain root at the end; run twice to verify replay.
```

## Full protection — what is locked

Every shipped binary in `bin/` is built and post-processed for STARGA's
production protection profile. See `protection/README.md` for the policy and
`protection/harden.sh` for the implementation.

**Compile-time (`Mind.toml [protection]`)**

- `obfuscate_strings` — string literals scrambled and decoded at use site.
- `anti_debug` — `mindc` inserts ptrace / TracerPid traps in the runtime path.
- `anti_tamper` — code-section integrity hashes verified at entry.
- `vm_protection` — hot kernels lowered through the MIND VM bytecode.

**Link-time**

- LTO + `--gc-sections` + `--strip-all`.
- `protection/exports.map` version script — only `mind_main` is dynamically
  exported from the runtime; everything else is forced LOCAL and cannot be
  reached via `dlsym`.
- `RPATH=$ORIGIN` — `xrm_mind_port` and `libxrmgov` find the bundled MIND
  runtime in the same directory; no `LD_LIBRARY_PATH` required.

**Post-link hardening (`protection/harden.sh`)**

- `.note.gnu.build-id`, `.note.gnu.property`, `.note.ABI-tag`,
  `.gnu.build.attributes`, `.comment` — all removed.
- `/rustc/<hash>/...`, `/home/n/...`, `~/.cargo/registry/...` strings in
  `.rodata` — zeroed out (no build-environment fingerprint leaks).
- `.comment` re-injected with a single deterministic line:
  `MIND: mind 0.2.3 (STARGA toolchain)`.
- All non-`mind_main` dynamic symbols on the runtime stripped via
  `objcopy --strip-symbol`.

**Runtime (Rust harness, `src/protection.rs`)**

- `/proc/self/status` `TracerPid != 0` ⇒ `exit(137)` (no diagnostic output).
- `LD_PRELOAD` / `LD_AUDIT` / `LD_PROFILE` / `LD_DEBUG` set ⇒ `exit(137)`.
- `prctl(PR_SET_DUMPABLE, 0)` — process is undumpable, no core, `/proc/self/mem`
  unreadable to non-root.
- Self-SHA-256 attestation printed at start of every run; matches the entry
  in `bin/SHA256SUMS`.

**Verification on every build**

`build.sh` runs `protection/harden.sh` automatically and fails the build if
any rustc / LLD / GCC / build-path string is still present in any shipped
binary, or if `.comment` is anything other than the MIND attribution line.

## What is inspectable

- `src/gov9.mind` — the 9 invariants as tensor reductions in MIND (source).
- `src/lib.mind` — kernel bootstrap (source).
- `src/main.rs` — same 9 invariants in Rust, with `PORTED_FROM:` comments
  mapping each function to its `gov9.mind` counterpart (source).
- `src/protection.rs` — the runtime guards listed above (source).
- `Mind.toml` — `[protection]` block (source).
- `protection/exports.map`, `protection/harden.sh`, `protection/README.md` —
  the link-time and post-link policy (source).
- `build.sh` — 7-stage build pipeline (source).
- `bin/SHA256SUMS` — sealed hashes of the three shipped binaries.

Nothing from XRM-SSD's core IP is touched. This harness measures governance
cost in isolation so it can be added to or subtracted from pipeline totals.

— STARGA, Inc. (`ceo@star.ga`)
