#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from datetime import datetime
from pathlib import Path

import numpy as np


PROGRESS_RE = re.compile(
    r"\[write-progress\]\s+timestamp=([\d.]+)\s+completed_keys=(\d+)"
)
ROUND_RE = re.compile(
    r"(\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}\.\d+).*?"
    r"\[TooFarBudgetAdaptive\]\s+round=(\d+)\s+"
    r"winning_global_slot=(\d+)\s+K/N=(\d+)/(\d+).*?"
    r"(?:win_bytes_per_sec|win_score)=([\d.eE+-]+).*?"
    r"exploit_sec=(\d+)\s+probe_slot_sec=(\d+)"
)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--progress-log", type=Path, required=True)
    parser.add_argument("--rocksdb-log", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    progress_text = args.progress_log.read_text(encoding="utf-8", errors="replace")
    progress = [
        (float(match.group(1)), int(match.group(2)))
        for match in PROGRESS_RE.finditer(progress_text)
    ]
    if len(progress) < 2:
        raise SystemExit("At least two progress samples are required.")
    progress_t = np.asarray([point[0] for point in progress])
    progress_k = np.asarray([point[1] for point in progress], dtype=float)

    rocksdb_text = args.rocksdb_log.read_text(encoding="utf-8", errors="replace")
    rows: list[list[object]] = []
    for match in ROUND_RE.finditer(rocksdb_text):
        timestamp = datetime.strptime(
            match.group(1), "%Y/%m/%d-%H:%M:%S.%f"
        ).timestamp()
        keys = float(
            np.interp(
                timestamp,
                progress_t,
                progress_k,
                left=progress_k[0],
                right=progress_k[-1],
            )
        )
        rows.append(
            [
                int(match.group(2)),
                int(match.group(3)),
                int(match.group(4)),
                int(match.group(5)),
                float(match.group(6)),
                int(match.group(7)),
                int(match.group(8)),
                keys / 1_000_000,
            ]
        )
    if not rows:
        raise SystemExit("No completed adaptive gear rounds were found.")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "round",
                "winning_global_slot",
                "winning_k",
                "winning_n",
                "winning_metric_value",
                "exploit_seconds",
                "probe_slot_seconds",
                "key_write_count_millions",
            ]
        )
        writer.writerows(rows)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
