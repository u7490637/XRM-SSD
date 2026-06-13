# XRM-SSD M2354 firmware — agent entrypoint

> This file is auto-loaded by the Fable 5 / Claude Code CLI when it opens this
> directory. It tells the agent what this project is and what to build.

## What this is

Build-ready C firmware for the **Nuvoton M2354** (Cortex-M23 / Armv8-M /
TrustZone-M) that runs the XRM-SSD **Runtime Graph Engine (RGE)** as a
deterministic, evidence-chained edge node. Each governed step blends node
state in Q16.16, checks 13 fail-closed invariants, and chains a SHA-256
`state_hash` over the on-chip crypto accelerator so the device can attest its
state matches an off-device reference.

The C here is the **reference contract**. It already runs and is hash-verified
against `host/rge_host.py` line-for-line on a dev box (no hardware needed).

## First thing to do when you open this repo

```sh
make test
```

This compiles the firmware for the host (portable SHA) and runs the Python
reference. **The per-epoch hashes must match line-for-line** — that equality
IS the attestation property. If they match, host and device agree. Confirm
this before changing anything.

## What you can build on top (in priority order)

1. **BSP integration.** `m2354/src/crypto_accel.c` has the canonical Nuvoton
   SHA-256 engine sequence behind `-DM2354_BSP`, but exact symbol names vary by
   BSP version. Wire it against the installed M2354 BSP and confirm the
   hardware digest equals the software fallback (so host attestation still
   holds). Build path: `make cross BSP_DIR=/path/to/Nuvoton/M2354/BSP`.

2. **TrustZone split.** Put the verify key + the hash-check in the secure
   world; expose a single non-secure entry that requests a verify and returns
   genuine/not — never lets the non-secure app read the key.

3. **Flash + on-device demo.** Link with the BSP startup + linker script,
   flash with NuLink / the on-board NuMaker debugger, run the 3-epoch loop on
   silicon, and confirm the on-device hashes match `host/rge_host.py`.

4. **CI parity gate.** A workflow that runs `make test` on every push and
   fails if the host↔device hashes diverge — keeps the contract honest.

## Hard rules — do not break these

- **Determinism is the whole point.** The blend uses an `int64_t` accumulator
  for `(w*a + inv*b) / 65536` (Q16.16). Don't switch to float, don't reorder
  the accumulation, don't change rounding. Any of those changes the hash.
- **The 13 invariants are fail-closed.** A violation must reject the mutation
  *before* it touches committed state. Don't soften a check into a warning.
- **The software SHA-256 (`sha256.c`) and the hardware engine must produce
  the identical digest.** That parity is what lets host/CI and silicon agree.
  Never let them drift.
- **The C mirrors the MIND contract in the parent repo field-for-field.** If
  you change the epoch byte layout, change it in lockstep with the host
  reference and document why.

## On the MIND compiler (read before promising it)

`mindc --target` is **cpu|gpu only** today (x86-64 / AArch64). There is **no
Armv8-M / Cortex-M backend yet**, so this firmware is hand-written C that
*matches* the MIND contract — not MIND-emitted code. The future milestone is
to cross-compile the Q16.16 RGE kernels for Armv8-M and prove they fold
byte-identical vs the host path, making the M2354 a third (secure) substrate
in the bit-identity proof. That requires building a Cortex-M codegen path
first. Until then, treat this C as the reference the eventual MIND output must
reproduce — don't claim a MIND→M2354 cross-compile that doesn't exist.

The actual MIND sources are in **`mind-contract/`**, and this C mirrors them
field-for-field:

- `rge.mind` — the VERIFY-ONLY contract: structs, bounds, node kinds, and all
  13 fail-closed invariant predicates. `rge.h` / `rge.c` mirror it 1:1.
- `blend_run.mind` — the pull-model blend, runnable. Folds to **32768**.
- `reduce_run.mind` — the N-edge weighted reduce (pre-rescale accumulator).
- `apply_run.mind` — the commit counters `[epoch, version] + 1`. Folds to **8**.
- `chain_run.mind` — the evidence-chain mix (deterministic stand-in for the
  SHA-256 anchor). Folds to **1615839279860409**.
- `m2354_kernel.mind` — the **v2 skeleton**: the whole commit step composed in
  the runnable surface, with the honest Armv8-M build-out checklist. This is
  the file the eventual Cortex-M backend would lower; the C is its reference.

Run a kernel on the host eval surface (folds to the value in its header):

```sh
cd mind-contract
mind eval --exec "$(grep -vE '^\s*//|^\s*$' blend_run.mind | tr '\n' ' ')"
```

The fold values (32768 / 8 / 1615839279860409) are exactly what the C firmware
reproduces — that equality is the host↔device byte-identity the demo rests on.

## Layout

```
m2354/include/rge.h            structs, Q16.16 bounds, 13 invariants
m2354/include/crypto_accel.h   SHA over the M2354 engine (or sw fallback)
m2354/include/sha256.h
m2354/src/rge.c                invariants, blend, evidence-chained commit
m2354/src/crypto_accel.c       BSP SHA engine path + portable fallback
m2354/src/sha256.c             portable SHA-256 (host/CI parity with silicon)
m2354/src/main.c               the 3-epoch governed-step demo loop
host/rge_host.py               host-side reference — identical bytes + hash
Makefile                       `make test` (host) / `make cross` (Armv8-M)
```
