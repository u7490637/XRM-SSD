#!/usr/bin/env python3
"""Assert that two MIND-port sweep runs produced identical chain roots
per intervention ratio.

The MIND port emits, for every ratio, a chain_root: Hash256 (hex).
Deterministic replay is the structural guarantee that two runs with the
same seed on the same (or different) hardware produce the same root.

Exit 0 = all five ratios match byte-for-byte.
Exit 2 = divergence; prints the ratio and both hashes so the hybrid
        / hardware source of the non-determinism can be tracked down.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def load(p: Path) -> dict:
    if not p.exists():
        print(f"ERROR: {p} not found", file=sys.stderr)
        sys.exit(2)
    with p.open() as f:
        return json.load(f)


def chain_roots_by_ratio(payload: dict) -> dict[str, str]:
    # Accept both the flat shape (compare.py consumer) and a richer
    # shape where MIND emits a list of per-ratio entries with chain_root.
    out: dict[str, str] = {}
    if "by_ratio" in payload and isinstance(payload["by_ratio"], list):
        for entry in payload["by_ratio"]:
            ratio = entry.get("ratio") or entry.get("ratio_label")
            chain = entry.get("chain_root")
            if ratio is None or chain is None:
                continue
            out[str(ratio)] = str(chain)
        return out
    # Flat form: chain roots under keys like "chain_root_5%"
    for k, v in payload.items():
        if k.startswith("chain_root_"):
            out[k.removeprefix("chain_root_")] = str(v)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("run_a", type=Path)
    ap.add_argument("run_b", type=Path)
    args = ap.parse_args()

    a = chain_roots_by_ratio(load(args.run_a))
    b = chain_roots_by_ratio(load(args.run_b))

    if not a or not b:
        print(
            "ERROR: could not find chain roots in one or both runs. "
            "Expected either by_ratio[].chain_root or chain_root_<ratio> keys.",
            file=sys.stderr,
        )
        return 2

    ratios = sorted(set(a) | set(b))
    diverged = []
    for r in ratios:
        ra = a.get(r)
        rb = b.get(r)
        if ra is None or rb is None or ra != rb:
            diverged.append((r, ra, rb))

    if diverged:
        print("FAIL: chain roots diverged across runs.", file=sys.stderr)
        for r, ra, rb in diverged:
            print(f"  ratio={r}", file=sys.stderr)
            print(f"    a: {ra}", file=sys.stderr)
            print(f"    b: {rb}", file=sys.stderr)
        return 2

    for r in ratios:
        print(f"  ratio={r}  root={a[r]}")
    print(f"OK: {len(ratios)} ratios match across both runs.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
