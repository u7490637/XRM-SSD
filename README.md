<p align="center">
  <img src="improved/docs/img/mind-logo.svg" alt="MIND" width="200">
</p>

# XRM-SSD

Public test-and-benchmark repository for XRM-SSD V23.3.

## Layout

- `Notebooks/` — Lightning AI L4 TensorRT-LLM test notebooks.
- `Test results/` — per-version benchmark reports.
- `improved/` — STARGA's MIND + XRM-SSD hybrid test harness (see
  `improved/README.md` and `improved/RUNBOOK_FOR_POLO.md`).
- `test_runner.py` — in-repo reproducibility driver.

## STARGA MIND integration

`improved/` contains a self-contained, bit-identical test harness that pairs
the XRM-SSD pipeline with STARGA's MIND governance kernel (9 invariants
evaluated as tensor reductions). Three artifacts ship prebuilt — the
`mindc`-compiled MIND kernel (`libxrmgov`), the bundled MIND runtime
(`libmind_cpu_linux-x64.so`), and a Rust bench harness (`xrm_mind_port`)
that reimplements the same nine invariants line-for-line. Running the Rust
bench twice produces a byte-identical SHA-256 evidence chain root.

All three shipped binaries are built with STARGA's full production
protection profile: `mindc [protection]` transforms (string obfuscation,
anti-debug, anti-tamper, VM bytecode), version-script export locking
(`mind_main` only), build-id / `.comment` / build-path scrubbing, and a
runtime guard layer (TracerPid, `LD_PRELOAD` / `LD_AUDIT` blocking,
`PR_SET_DUMPABLE=0`, self-SHA-256 attestation). See
`improved/protection/README.md`.

### Quick run

```bash
cd improved/bin
./xrm_mind_port --iter 100000
```

Full details in `improved/RUNBOOK_FOR_POLO.md`.

## License

Repository is test-only. Do not use for commercial redistribution or
modification. The MIND binary in `improved/bin/` is STARGA-proprietary;
see `improved/README.md`.
