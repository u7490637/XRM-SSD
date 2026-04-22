# protection/

Hardening pass applied on top of the standard `mindc` + `cargo` build. Runs
automatically as stage 7 of `../build.sh`; can also be re-run stand-alone
against an existing `bin/` directory:

    ./protection/harden.sh

## What gets protected

### `bin/libxrmgov`  (mindc-compiled MIND kernel)
- `mindc 0.2.3` produces a stripped, RPATH=`$ORIGIN` ELF out of the box.
- `build.sh` additionally:
  - Strips every `mind_module_*_get_source` / `get_ir` symbol from the
    symbol table (Stage 2).
  - Zeroes every ASCII string in `.rodata` that matches MIND source
    syntax, build paths, or internal IR markers (Stage 3).
  - Overwrites `.comment` with a single deterministic MIND attribution
    line (Stage 4).
- `harden.sh` additionally:
  - Removes `.note.gnu.build-id`, `.note.gnu.property`, `.note.ABI-tag`.
  - Normalises `.comment` to `MIND: mind 0.2.3 (STARGA toolchain)`.

After hardening, `libxrmgov` exposes zero user-visible symbols. `nm -D`
returns empty.

### `bin/libmind_cpu_linux-x64.so`  (MIND runtime)
- Bundled next to `libxrmgov` so `RPATH=$ORIGIN` resolves without
  `LD_LIBRARY_PATH`.
- `harden.sh` enforces:
  - **One exported symbol** (`mind_main`). Every other dynamic symbol is
    stripped from the symbol table via `objcopy --strip-symbol`. This is
    the post-hoc equivalent of applying
    `exports.map`:
    ```
    MIND_CPU_1.0 { global: mind_main; local: *; };
    ```
  - `.note.gnu.*` sections removed.
  - `.rodata` build-path strings (`/home/n/mind/src/...`) zeroed out.
  - `.comment` normalised.

### `bin/xrm_mind_port`  (Rust bench harness)
- Cargo release profile: `strip = true`, `lto = "fat"`, `panic = "abort"`,
  `opt-level = 3`.
- `harden.sh` additionally:
  - Strips `.comment` (which by default leaks the full `rustc` / `LLD` /
    `GCC` version triplet).
  - Removes `.note.gnu.build-id` / `.note.gnu.property` /
    `.note.ABI-tag`.
  - Zeroes every `/rustc/<hash>/library/...` string baked into `.rodata`
    by rustc's panic-location tables.
  - Replaces `.comment` with the same MIND attribution line as the other
    two binaries.

## What is *not* in this directory

The STARGA-internal protection source (NikolaChess-class `protection.mind` /
`protection.c` runtime guards: anti-debug, anti-tamper, VM bytecode,
triple-redundant state, watchdog) is **not shipped in this repo**. It lives
in a private STARGA tree. `build.sh` does not invoke it and `harden.sh`
does not depend on it. Everything in this directory is reproducible with a
stock `mindc` + `cargo` + `binutils` + `patchelf`.

## Verification

`harden.sh` fails the build if any of the following patterns survive in
any shipped binary:

    rustc version [0-9]
    LLD [0-9]+
    Ubuntu [0-9]+\.[0-9]+\.[0-9]+
    GCC: \(
    /home/
    \.cargo/registry
    /rustc/

The smoke output on a clean build:

    [OK] no residual rustc / LLD / GCC / path strings in any shipped binary

followed by an enumeration of every dynamic symbol and `.comment` string.
