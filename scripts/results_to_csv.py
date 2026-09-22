#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


THROUGHPUT_RE = re.compile(
    r"randomwrite\s*:\s*[\d.]+\s+micros/op\s+([\d.]+)\s+Mops/s"
)
KNOWN_WORKLOADS = ("uniform", "s0.5", "s1.5", "s3")


def throughput(path: Path) -> float:
    text = path.read_text(encoding="utf-8", errors="replace")
    if "Write ERROR" in text or "Write FAILED" in text or "No space left" in text:
        raise ValueError(f"Failed run: {path}")
    values = THROUGHPUT_RE.findall(text)
    if len(values) != 1:
        raise ValueError(f"Expected one throughput result in {path}, found {len(values)}")
    return float(values[0]) * 1000


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--workloads", default=",".join(KNOWN_WORKLOADS))
    args = parser.parse_args()

    rows: list[list[str]] = []
    workloads = tuple(value.strip() for value in args.workloads.split(",") if value.strip())
    unknown = set(workloads) - set(KNOWN_WORKLOADS)
    if unknown:
        parser.error(f"Unknown workloads: {', '.join(sorted(unknown))}")
    for workload in workloads:
        base = args.results_root / workload
        rows.append(
            [
                "Uniform" if workload == "uniform" else f"Zipf_{workload}",
                f"{throughput(base / 'nofdp' / 'nofdp_phase2.log'):.3f}",
                f"{throughput(base / 'baseline' / 'baseline_phase2.log'):.3f}",
                f"{throughput(base / 'model' / 'model_phase2.log'):.3f}",
            ]
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "workload",
                "rocksdb_nofdp_kops",
                "rocksdb_fdp_kops",
                "fdp_rocks_kops",
            ]
        )
        writer.writerows(rows)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
