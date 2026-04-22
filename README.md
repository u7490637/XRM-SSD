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
evaluated as tensor reductions). Two binaries ship prebuilt — one compiled
by `mindc` from MIND source, one in Rust reimplementing the same nine
invariants line-for-line. Running the Rust bench twice produces a
byte-identical SHA-256 evidence chain root.

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
