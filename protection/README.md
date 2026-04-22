# Protection — MIND Port Locked Build

STARGA's MIND runtime libraries (`libmind_cpu_*`, `libmind_cuda_*`,
governance kernel, evidence chain, Q16.16 primitives) are proprietary
and are **never shipped as source** in this public repo.

When `make build` runs in `test-harness/`, those libraries are linked
into the final ELF under the full protection transform set described
in `Mind.toml`. The compiled binary is what ships; the runtime source
stays behind the STARGA toolchain.

## What protects the final binary

### Compiler transforms (mindc, four transforms)

| Transform            | What it does |
|----------------------|--------------|
| `obfuscate_strings`  | All string literals encrypted; decrypted on-demand at use sites. |
| `anti_debug`         | `ptrace` / `IsDebuggerPresent` / `PT_DENY_ATTACH` checks inserted at entry and in watchdog. |
| `anti_tamper`        | Binary integrity hash verified on entry; mismatch → abort. |
| `vm_protection`      | Critical code paths (seal verify, FFI dispatch, governance aggregation) lowered to a custom VM bytecode interpreter in the ELF. |

### Runtime protections (always-on for `[protection]` modules)

1. `ptrace_guard` — active ptrace detection, trap on attach
2. `debug_register_guard` — DR0-DR7 inspection
3. `timing_anomaly_guard` — RDTSC deltas, abort on single-step
4. `integrity_self_check` — continuous hash of `.text`
5. `watchdog_loop` — background thread verifying 1-4
6. `triple_redundant_state` — critical state stored three times, majority vote
7. `encrypted_string_table` — AES-lite on the const pool
8. `indirect_jump_table` — direct jumps replaced with dispatched indices
9. `cflow_flattening` — control flow reshaped to a state machine
10. `dead_code_insertion` — junk inlined around hot paths
11. `opaque_predicate` — unreachable branches the analyzer must prove away
12. `constant_encryption` — numeric constants obfuscated
13. `symbol_obfuscation` — internal names mangled
14. `stack_guard` — canary around every call frame

### Linker-level controls (`exports.map`)

Only five symbols are externally visible:

```
xrm_mind_port_main
xrm_mind_port_run_sweep
xrm_mind_port_version
xrm_mind_port_seal_ok
```

Everything else — including `kernel_*`, `invariant_*`, `gov9_*`,
`verify_blob_seal*`, `xrm_reflect*`, `dispatch*`, and all
`mind_runtime_*` / `mind_rt_*` / `mindc_*` internals — is forced
LOCAL and never appears in the dynamic symbol table. No `get_source`,
no `_mind_source_*`, no `_mind_debug_*` escape.

### Strip phase

- Source-embedding sections removed (`strip_source`)
- Full symbol table dropped (`strip_symbols`)
- `.comment` cleared so the ELF carries no toolchain fingerprint
- `gc_sections` removes every unreferenced section

## Sealed blob policy

The XRM-SSD V23.3 reflection blob (provided by Polo) is **not** decoded
by MIND. Its SHA-256 is baked in at compile time via
`test-harness/seal_hashes.inc`. `verify_blob_seal()` runs before every
`xrm_reflect()` dispatch; a single byte change to the blob after the
build aborts the dispatch path.

## What this repo does and doesn't contain

| | Shipped here | Shipped elsewhere |
|-|---|---|
| MIND source (`.mind`) for the port harness | Yes, `examples/xrm_mind_port.mind` | — |
| STARGA MIND runtime source | No | STARGA internal |
| STARGA MIND compiler (`mindc`) | No | STARGA internal toolchain |
| XRM-SSD V23.3 reflection source | No | Dollarchip private |
| XRM-SSD reflection blob binary | No | Polo (under NDA) |
| Protection manifest (`Mind.toml`) | Yes | — |
| Linker version script (`exports.map`) | Yes | — |
| Test harness (Makefile, compare.py) | Yes | — |

The public surface is: "how to run the test and what the binary's
protection profile looks like." The runtime and the XRM core stay
private.
