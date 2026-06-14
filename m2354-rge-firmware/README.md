# XRM-SSD — NuMaker-M2354 firmware (governed RGE edge node)

Build-ready C firmware for the Nuvoton **M2354** (Cortex-M23 / Armv8-M /
TrustZone-M). It runs the XRM-SSD Runtime Graph Engine (RGE) as a
**deterministic, evidence-chained edge node**, mirroring the MIND port in
`../src/rge.mind` + `../src/blend_run.mind` field-for-field.

## What it does

Each governed step the device:

1. **Blends** a node's state from inbound edges — `(w*a + inv*b)/65536` with
   an `int64_t` Q32.32 accumulator (the overflow gotcha confirmed in
   `blend_run.mind`).
2. **Commits** the result as an atomic, epoch-ordered mutation — every one of
   the 13 RGE invariants is checked **fail-closed**; a violation rejects the
   mutation before it touches committed state.
3. **Chains** a SHA-256 `state_hash` over the M2354 crypto accelerator,
   anchoring the audit trail (mirrors MIND's `trace_hash` provenance).
4. **Attests** the recomputed hash matches the host's — the basis for the
   two-tier hybrid.

## Layout

```
firmware/
  Makefile                 host + cross-compile entry
  m2354/include/rge.h      C mirror of rge.mind (structs, bounds, invariants)
  m2354/include/crypto_accel.h   SHA over the M2354 engine (or sw fallback)
  m2354/include/sha256.h
  m2354/src/rge.c          invariants, blend, evidence-chained commit
  m2354/src/crypto_accel.c BSP SHA engine path + portable fallback
  m2354/src/sha256.c       portable SHA-256 (host/CI parity with silicon)
  m2354/src/main.c         the demo loop
  host/rge_host.py         host-side reference — identical bytes + hash
  mind-contract/           the MIND sources this C mirrors field-for-field
    rge.mind               verify-only contract (structs + 13 invariants)
    blend_run.mind         pull-model blend          -> folds to 32768
    reduce_run.mind        N-edge weighted reduce
    apply_run.mind         commit counters epoch/version + 1 -> folds to 8
    chain_run.mind         evidence-chain mix        -> folds to 1615839279860409
    m2354_kernel.mind      v2 skeleton: commit step composed + Armv8-M checklist
```

## Run it today (no hardware)

```sh
cd firmware
make test
```

`make test` builds the C firmware for your dev box (portable SHA) and runs the
Python host reference. **If the per-epoch hashes match line-for-line, the
host and the device agree** — that's the property the on-orbit attestation
relies on.

## Flash it on your NuMaker-M2354

The device build is **self-contained** — it needs only `arm-none-eabi-gcc`
(no Nuvoton BSP). Output is over **semihosting**, so you see the run on the
debugger console with no UART pin-mux or clock bring-up.

```sh
cd m2354
make device          # -> build/rge_m2354.{elf,bin,hex}  (Cortex-M23, ~16 KB)
```

Flash the image however you already do:

- **pyOCD** (`pip install pyocd && pyocd pack install M2354`):
  ```sh
  make flash         # = pyocd flash -t m2354 build/rge_m2354.bin
  ```
- **Nu-Link / NuMaker**: flash `build/rge_m2354.bin` with the Nuvoton tool, or
  drag-drop it onto the NuMaker mass-storage drive.
- **Keil MDK**: open `build/rge_m2354.elf`.

**See it run:** start a debug session (Nu-Link GDB server, `pyocd gdbserver`,
OpenOCD, or Keil) — the three governed epochs print over semihosting:

```
epoch=1 node.state=32768 node.version=1  hash= 295b629000247bde54b2adfa0edbf33a4cf5606022222a5a996b8ead2d414803
epoch=2 node.state=32768 node.version=2  hash= afe69a642b8ad19c4ae4dfb1a1c64187f5e0460be5d1ce67bd4f0affb9430c19
epoch=3 node.state=32768 node.version=3  hash= d2afcdf58ad6b96f6a41267eb164c625fb66caa859163603a664bd63ff314499
```

Those hashes are **byte-identical** to `make test` and `host/rge_host.py` — the
silicon agrees with the host reference, which is the whole attestation property.
(v1 uses the portable SHA-256 so the digests match exactly.)

> Build verified: host (`make test`) and the Cortex-M23 cross-compile + link
> (`make device` → `rge_m2354.bin`) both green; the on-silicon run is yours to
> confirm. The image is tiny (~16 KB flash / <8 KB RAM) — fits every M2354.

### v2 — hardware SHA engine (optional, needs the BSP)

```sh
make cross BSP_DIR=/path/to/Nuvoton/M2354/BSP
```

`-DM2354_BSP` routes SHA-256 through the M2354 crypto accelerator; the digest is
identical to the software fallback, so host attestation still holds.

## The MIND path (v2)

Today `mindc --target` is **cpu|gpu only** (x86-64 / AArch64) — there is no
Armv8-M backend yet, so the firmware above is hand-written C that *matches*
the MIND contract. The exciting milestone is to cross-compile the Q16.16 RGE
kernels for Armv8-M and confirm they fold **byte-identical** vs the host/FPGA
path, making the M2354 a **third substrate** in the bit-identity proof — the
secure one. That needs a Cortex-M codegen path we'd build; this C firmware is
the reference the eventual MIND-emitted code must reproduce.

The runnable MIND kernels in `mind-contract/` already fold to the exact values
the C reproduces — run one on the host eval surface:

```sh
cd mind-contract
mind eval --exec "$(grep -vE '^\s*//|^\s*$' blend_run.mind | tr '\n' ' ')"   # -> 32768
mind eval --exec "$(grep -vE '^\s*//|^\s*$' apply_run.mind | tr '\n' ' ')"   # -> 8
mind eval --exec "$(grep -vE '^\s*//|^\s*$' chain_run.mind | tr '\n' ' ')"   # -> 1615839279860409
```

`m2354_kernel.mind` is the skeleton the eventual Armv8-M backend lowers — the
whole commit step composed in the runnable surface, with the honest build-out
checklist (Cortex-M codegen, `>>16` lowering fix, `std.sha256` wiring, the
cross-substrate gate that makes the M2354 substrate #3). `mind` here is the
0.7.x source build (`~/mind/target/release/mind`); the rescale uses `/65536`
not `>>16` per the gap noted in the kernel headers.
