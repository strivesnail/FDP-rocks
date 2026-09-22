#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

import numpy as np


USER_RE = re.compile(r"User bytes written \(ingest\):\s+(\d+)")
FLUSH_RE = re.compile(r"Flush bytes written:\s+(\d+)")
COMPACT_RE = re.compile(r"Compaction bytes written:\s+(\d+)")
PROGRESS_RE = re.compile(
    r"\[write-progress\]\s+timestamp=([\d.]+)\s+completed_keys=(\d+)"
)
HBMW_RE = re.compile(r"HBMW\):\s+([\d,]+)")
MBMW_RE = re.compile(r"MBMW\):\s+([\d,]+)")


def wa_blocks(path: Path) -> list[tuple[int, int, int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    blocks: list[tuple[int, int, int]] = []
    position = 0
    while True:
        start = text.find("User bytes written (ingest):", position)
        if start < 0:
            break
        chunk = text[start : start + 700]
        user = USER_RE.search(chunk)
        flush = FLUSH_RE.search(chunk)
        compact = COMPACT_RE.search(chunk)
        if user and flush and compact:
            blocks.append(
                (int(user.group(1)), int(flush.group(1)), int(compact.group(1)))
            )
        position = start + 1
    if not blocks or blocks[-1][0] <= 0:
        raise ValueError(f"No valid write-amplification samples in {path}")
    return blocks


def wa_series(path: Path, base_keys: int, written_keys: int) -> tuple[np.ndarray, np.ndarray]:
    blocks = wa_blocks(path)
    final_user = blocks[-1][0]
    x = np.asarray(
        [base_keys + written_keys * user / final_user for user, _, _ in blocks],
        dtype=float,
    )
    y = np.asarray(
        [(flush + compact) / user for user, flush, compact in blocks],
        dtype=float,
    )
    return x / 1_000_000, y


def progress_points(*paths: Path) -> list[tuple[float, int]]:
    points: list[tuple[float, int]] = []
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        points.extend(
            (float(match.group(1)), int(match.group(2)))
            for match in PROGRESS_RE.finditer(text)
        )
    if not points:
        raise ValueError("No machine-readable progress samples found")
    return sorted(points)


def device_samples(path: Path) -> list[tuple[float, int, int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    samples: list[tuple[float, int, int]] = []
    for chunk in text.split("SAMPLE timestamp=")[1:]:
        timestamp, _, rest = chunk.partition("\n")
        hbmw = HBMW_RE.search(rest)
        mbmw = MBMW_RE.search(rest)
        if hbmw and mbmw:
            samples.append(
                (
                    float(timestamp.strip()),
                    int(hbmw.group(1).replace(",", "")),
                    int(mbmw.group(1).replace(",", "")),
                )
            )
    if len(samples) < 2:
        raise ValueError(f"Insufficient FDP samples in {path}")
    return sorted(samples)


def dlwa_series(
    progress: list[tuple[float, int]],
    samples: list[tuple[float, int, int]],
) -> tuple[np.ndarray, np.ndarray]:
    progress_t = np.asarray([point[0] for point in progress])
    progress_k = np.asarray([point[1] for point in progress], dtype=float)
    sample_t = np.asarray([sample[0] for sample in samples])
    hbmw = np.asarray([sample[1] for sample in samples], dtype=float)
    mbmw = np.asarray([sample[2] for sample in samples], dtype=float)
    keys = np.interp(sample_t, progress_t, progress_k, left=0, right=progress_k[-1])
    host_delta = hbmw - hbmw[0]
    media_delta = mbmw - mbmw[0]
    valid = (host_delta > 1024 * 1024) & (keys > 0)
    return (
        np.concatenate(([0.0], keys[valid] / 1_000_000)),
        np.concatenate(([1.0], media_delta[valid] / host_delta[valid])),
    )


def interpolate(x: np.ndarray, y: np.ndarray, targets: np.ndarray) -> np.ndarray:
    order = np.argsort(x, kind="stable")
    unique_x: list[float] = []
    unique_y: list[float] = []
    for xv, yv in zip(x[order], y[order]):
        if unique_x and abs(float(xv) - unique_x[-1]) < 1e-9:
            unique_y[-1] = float(yv)
        else:
            unique_x.append(float(xv))
            unique_y.append(float(yv))
    return np.interp(targets, np.asarray(unique_x), np.asarray(unique_y))


def mode_series(
    directory: Path,
    mode: str,
    phase1_keys: int,
    phase2_keys: int,
    targets: np.ndarray,
) -> tuple[np.ndarray, np.ndarray]:
    phase1 = directory / mode / f"{mode}_phase1.log"
    phase2 = directory / mode / f"{mode}_phase2.log"
    stats = directory / mode / "fdp_stats.log"
    x1, y1 = wa_series(phase1, 0, phase1_keys)
    x2, y2 = wa_series(phase2, phase1_keys, phase2_keys)
    alwa = interpolate(
        np.concatenate((x1, x2)),
        np.concatenate((y1, y2)),
        targets,
    )
    dx, dy = dlwa_series(
        progress_points(phase1, phase2),
        device_samples(stats),
    )
    return alwa, interpolate(dx, dy, targets)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workload-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase1-keys", type=int, default=50_000_000)
    parser.add_argument("--phase2-keys", type=int, default=100_000_000)
    parser.add_argument("--step-keys", type=int, default=5_000_000)
    args = parser.parse_args()

    total = args.phase1_keys + args.phase2_keys
    targets = np.arange(0, total + 1, args.step_keys, dtype=float) / 1_000_000
    baseline_alwa, baseline_dlwa = mode_series(
        args.workload_dir,
        "baseline",
        args.phase1_keys,
        args.phase2_keys,
        targets,
    )
    model_alwa, model_dlwa = mode_series(
        args.workload_dir,
        "model",
        args.phase1_keys,
        args.phase2_keys,
        targets,
    )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            [
                "key_write_count_millions",
                "alwa_fdp_rocks",
                "alwa_rocksdb_fdp",
                "dlwa_fdp_rocks",
                "dlwa_rocksdb_fdp",
            ]
        )
        writer.writerows(
            zip(targets, model_alwa, baseline_alwa, model_dlwa, baseline_dlwa)
        )
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
