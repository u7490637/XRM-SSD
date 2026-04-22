#!/usr/bin/env python3
"""Produce a comparison table between the MIND-native port and
the Rust/Python hybrid (main.rs) bench runs.

Does NOT fabricate numbers. Reads JSON produced by each harness
and emits a markdown table with raw TPS values only. If one side
is missing, it prints 'not measured' — not a guess.
"""
import argparse
import json
import sys
from pathlib import Path


def load(path: Path):
    if not path.exists():
        return None
    with path.open() as f:
        return json.load(f)


def row(ratio_label: str, mind_tps, hybrid_tps):
    def fmt(v):
        return f"{v:,.0f}" if isinstance(v, (int, float)) else "not measured"

    ratio_mind = mind_tps.get(ratio_label) if mind_tps else None
    ratio_hyb = hybrid_tps.get(ratio_label) if hybrid_tps else None
    speedup = "—"
    if isinstance(ratio_mind, (int, float)) and isinstance(ratio_hyb, (int, float)) and ratio_hyb > 0:
        speedup = f"{ratio_mind / ratio_hyb:.2f}x"
    return f"| {ratio_label} | {fmt(ratio_hyb)} | {fmt(ratio_mind)} | {speedup} |"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mind", type=Path, required=True)
    ap.add_argument("--hybrid", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    mind = load(args.mind)
    hybrid = load(args.hybrid)

    lines = [
        "# MIND Port vs Hybrid — Measured TPS",
        "",
        "Both sides run the same gov9 + XRM-reflection path on the same L4 hardware.",
        "The hybrid path goes through PyO3 → Rust cdylib → XRM. The MIND path replaces",
        "PyO3 + cdylib with a single mindc-built ELF that calls the sealed XRM blob",
        "via a typed extern FFI.",
        "",
        "| Intervention ratio | Hybrid (main.rs) TPS | MIND port TPS | Speedup |",
        "|--------------------|----------------------|---------------|---------|",
    ]
    for r in ["1%", "5%", "10%", "25%", "50%"]:
        lines.append(row(r, mind, hybrid))

    lines.append("")
    lines.append("**Method.** TPS = iterations / wall-clock elapsed. iterations=1,000,000")
    lines.append("per ratio. Warm-up=80 iterations discarded. Median of 5 runs per ratio.")
    lines.append("")
    if mind is None:
        lines.append("⚠ MIND port has not been measured on this host. Run `make run`.")
    if hybrid is None:
        lines.append("⚠ Hybrid baseline has not been measured on this host. Run `make run-hybrid`.")

    args.out.write_text("\n".join(lines))
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    sys.exit(main())
