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

## Flash to the M2354

```sh
make cross BSP_DIR=/path/to/Nuvoton/M2354/BSP
```

Then link the objects with your BSP startup + linker script and flash with
NuLink / the on-board NuMaker debugger. `-DM2354_BSP` routes SHA-256 through
the hardware engine; the digest is identical to the software fallback, so
host attestation still holds.

## The MIND path (v2)

Today `mindc --target` is **cpu|gpu only** (x86-64 / AArch64) — there is no
Armv8-M backend yet, so the firmware above is hand-written C that *matches*
the MIND contract. The exciting milestone is to cross-compile the Q16.16 RGE
kernels for Armv8-M and confirm they fold **byte-identical** vs the host/FPGA
path, making the M2354 a **third substrate** in the bit-identity proof — the
secure one. That needs a Cortex-M codegen path we'd build; this C firmware is
the reference the eventual MIND-emitted code must reproduce.
