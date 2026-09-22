#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path

import numpy as np

EVENT_RE = re.compile(r"EVENT_LOG_v1\s+(\{.*\})")
MOVE_RE = re.compile(r"Moved #(\d+) to level-(\d+) \d+ bytes")


def parse_log(path: Path, use_move_destination: bool) -> dict[int, list[float]]:
    created: dict[int, tuple[int, int]] = {}
    deleted: dict[int, int] = {}
    moved: set[int] = set()
    move_destination: dict[int, int] = {}
    final_time = 0

    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            move = MOVE_RE.search(line)
            if move:
                file_number = int(move.group(1))
                moved.add(file_number)
                move_destination[file_number] = int(move.group(2))
                continue
            event_match = EVENT_RE.search(line)
            if not event_match:
                continue
            try:
                event = json.loads(event_match.group(1))
            except json.JSONDecodeError:
                continue
            timestamp = event.get("time_micros")
            if timestamp is not None:
                timestamp = int(timestamp)
                final_time = max(final_time, timestamp)
            if event.get("event") == "trivial_move":
                destination = event.get("destination_level")
                if destination is not None:
                    for file_number in event.get("input_files") or []:
                        file_number = int(file_number)
                        moved.add(file_number)
                        move_destination[file_number] = int(destination)
                continue
            file_number = event.get("file_number")
            if file_number is None or timestamp is None:
                continue
            file_number = int(file_number)
            if event.get("event") == "table_file_creation":
                level = event.get("level")
                if level is not None and file_number not in created:
                    created[file_number] = (timestamp, int(level))
            elif event.get("event") == "table_file_deletion":
                deleted[file_number] = timestamp

    lifetimes: dict[int, list[float]] = defaultdict(list)
    for file_number, (created_at, level) in created.items():
        if use_move_destination:
            level = move_destination.get(file_number, level)
        elif file_number in moved:
            continue
        if level not in range(1, 6):
            continue
        deleted_at = deleted.get(file_number, final_time)
        lifetime = (deleted_at - created_at) / 1_000_000
        if lifetime >= 0:
            lifetimes[level].append(lifetime)
    return dict(lifetimes)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--percentile", type=float, default=25.0)
    parser.add_argument("--trivial-dest-level", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    if not args.log.is_file():
        parser.error(f"Log does not exist: {args.log}")
    values = parse_log(args.log, args.trivial_dest_level)
    thresholds: list[float] = []
    for level in range(1, 6):
        samples = np.asarray(values.get(level, []), dtype=float)
        if samples.size == 0:
            raise SystemExit(f"No lifetime samples for L{level}")
        threshold = float(np.percentile(samples, args.percentile))
        thresholds.append(threshold)
        print(f"L{level}: n={samples.size} p{args.percentile:g}={threshold:.2f}s")

    result = ",".join(f"{value:.2f}" for value in thresholds)
    print(f"ROCKSDB_TOO_FAR_THRESHOLDS_SEC={result}")
    if args.output:
        args.output.write_text(result + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
