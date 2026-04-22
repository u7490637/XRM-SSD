#!/usr/bin/env python3
"""Parse stdout from the existing xrm_ssd_v23_3_integration/main.rs run
into a stable JSON shape that compare.py can consume.

Input: a log file containing lines like
    XRM intervention ratio: 5% => Overall System TPS: 445137.28

Output: JSON with ratio -> TPS, plus minimal provenance.

Keeps Polo's main.rs untouched. If the line format ever changes, edit
the regex here, not the Rust.
"""
from __future__ import annotations

import argparse
import json
import platform
import re
import socket
import sys
from datetime import datetime, timezone
from pathlib import Path

LINE_RE = re.compile(
    r"XRM\s+intervention\s+ratio:\s*(\d+(?:\.\d+)?)\s*%\s*"
    r"=>\s*Overall\s+System\s+TPS:\s*([0-9]+(?:\.[0-9]+)?)",
    re.IGNORECASE,
)


def parse_log(text: str) -> dict[str, int]:
    out: dict[str, int] = {}
    for m in LINE_RE.finditer(text):
        raw = m.group(1)
        if "." in raw:
            # Only strip trailing zeros on fractional values (e.g. "5.00" -> "5").
            ratio = raw.rstrip("0").rstrip(".") or "0"
        else:
            ratio = raw  # integer ratios unchanged ("10" stays "10").
        tps = float(m.group(2))
        out[f"{ratio}%"] = int(round(tps))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("log", type=Path, help="hybrid stdout capture (text)")
    ap.add_argument("out", type=Path, help="path to write JSON")
    ap.add_argument(
        "--iterations",
        type=int,
        default=1_000_000,
        help="iterations per ratio (default 1,000,000 — match main.rs)",
    )
    args = ap.parse_args()

    if not args.log.exists():
        print(f"ERROR: log not found: {args.log}", file=sys.stderr)
        return 2

    text = args.log.read_text()
    results = parse_log(text)
    if not results:
        print(
            "ERROR: no XRM intervention lines parsed from log.\n"
            "Expected lines of the form:\n"
            "  XRM intervention ratio: N% => Overall System TPS: X",
            file=sys.stderr,
        )
        return 3

    payload = {
        "source": "hybrid_main_rs",
        "host": socket.gethostname(),
        "platform": platform.platform(),
        "timestamp_utc": datetime.now(timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z"),
        "iterations_per_ratio": args.iterations,
        "log_file": str(args.log),
        **results,
    }
    args.out.write_text(json.dumps(payload, indent=2, sort_keys=True))
    print(f"Parsed {len(results)} ratios → {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
