#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path

import numpy as np


PROGRESS_RE = re.compile(
    r"\[timestamp=([\d.]+)\]\s*\|\s*[^:]+:\s*(\d+)"
)
USER_RE = re.compile(r"User bytes written \(ingest\):\s+(\d+)")
FLUSH_RE = re.compile(r"Flush bytes written:\s+(\d+)")
COMPACT_RE = re.compile(r"Compaction bytes written:\s+(\d+)")
HBMW_RE = re.compile(r"HBMW\):\s+([\d,]+)")
MBMW_RE = re.compile(r"MBMW\):\s+([\d,]+)")
PHASE2_RE = re.compile(r"^.*2:\s*Phase2\s+-", re.MULTILINE)


def phase2_position(text: str) -> int:
    match = PHASE2_RE.search(text)
    if not match:
        raise ValueError("Phase2 marker not found")
    return match.start()


def wa_blocks(text: str) -> list[tuple[int, int, int, int]]:
    blocks: list[tuple[int, int, int, int]] = []
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
                (start, int(user.group(1)), int(flush.group(1)), int(compact.group(1)))
            )
        position = start + 1
    return blocks


def last_progress(text: str, phase2: bool) -> int:
    split = phase2_position(text)
    part = text[split:] if phase2 else text[:split]
    values = [int(match.group(2)) for match in PROGRESS_RE.finditer(part)]
    if not values:
        raise ValueError("No progress samples found")
    return values[-1]


def scaled_wa(
    blocks: list[tuple[int, int, int, int]],
    base_keys: int,
    written_keys: int,
    offset: tuple[int, int, int] = (0, 0, 0),
) -> tuple[np.ndarray, np.ndarray, tuple[int, int, int]]:
    if not blocks or blocks[-1][1] <= 0:
        raise ValueError("No valid write-amplification blocks")
    final_user = blocks[-1][1]
    off_user, off_flush, off_compact = offset
    x: list[float] = []
    y: list[float] = []
    for _, user, flush, compact in blocks:
        total_user = off_user + user
        total_written = off_flush + flush + off_compact + compact
        x.append((base_keys + written_keys * user / final_user) / 1_000_000)
        y.append(total_written / total_user)
    last = blocks[-1]
    return (
        np.asarray(x),
        np.asarray(y),
        (off_user + last[1], off_flush + last[2], off_compact + last[3]),
    )


def complete_alwa(
    path: Path, phase1_keys: int
) -> tuple[np.ndarray, np.ndarray]:
    text = path.read_text(encoding="utf-8", errors="replace")
    split = phase2_position(text)
    blocks = wa_blocks(text)
    phase1 = [block for block in blocks if block[0] < split]
    phase2 = [block for block in blocks if block[0] >= split]
    x1, y1, _ = scaled_wa(phase1, 0, last_progress(text, False))
    x2, y2, _ = scaled_wa(
        phase2,
        phase1_keys,
        last_progress(text, True),
    )
    return np.concatenate((x1, x2)), np.concatenate((y1, y2))


def segmented_alwa(
    initial: Path,
    resumes: list[Path],
    phase1_keys: int,
    phase2_keys: int,
) -> tuple[np.ndarray, np.ndarray]:
    text = initial.read_text(encoding="utf-8", errors="replace")
    split = phase2_position(text)
    blocks = wa_blocks(text)
    phase1 = [block for block in blocks if block[0] < split]
    phase2 = [block for block in blocks if block[0] >= split]
    x1, y1, _ = scaled_wa(phase1, 0, last_progress(text, False))
    completed = last_progress(text, True)
    x2, y2, offset = scaled_wa(phase2, phase1_keys, completed)
    x_parts = [x1, x2]
    y_parts = [y1, y2]

    for path in resumes:
        resume_text = path.read_text(encoding="utf-8", errors="replace")
        progress = [int(match.group(2)) for match in PROGRESS_RE.finditer(resume_text)]
        if not progress:
            raise ValueError(f"No resume progress in {path}")
        end = progress[-1]
        x, y, offset = scaled_wa(
            wa_blocks(resume_text),
            phase1_keys + completed,
            end - completed,
            offset,
        )
        x_parts.append(x)
        y_parts.append(y)
        completed = end
    if completed != phase2_keys:
        raise ValueError(f"Segmented Phase2 ended at {completed}, expected {phase2_keys}")
    return np.concatenate(x_parts), np.concatenate(y_parts)


def progress(path: Path, phase1_keys: int, phase2_only: bool = False) -> list[tuple[float, int]]:
    text = path.read_text(encoding="utf-8", errors="replace")
    split = phase2_position(text) if not phase2_only else -1
    points: list[tuple[float, int]] = []
    for match in PROGRESS_RE.finditer(text):
        keys = int(match.group(2))
        if phase2_only or (split >= 0 and match.start() >= split):
            keys += phase1_keys
        points.append((float(match.group(1)), keys))
    return points


def device_samples(paths: list[Path]) -> list[tuple[float, int, int]]:
    samples: list[tuple[float, int, int]] = []
    for path in paths:
        text = path.read_text(encoding="utf-8", errors="replace")
        for chunk in text.split("SAMPLE timestamp=")[1:]:
            timestamp, _, rest = chunk.partition("\n")
            host = HBMW_RE.search(rest)
            media = MBMW_RE.search(rest)
            if host and media:
                samples.append(
                    (
                        float(timestamp.strip()),
                        int(host.group(1).replace(",", "")),
                        int(media.group(1).replace(",", "")),
                    )
                )
    return sorted(samples)


def dlwa(
    points: list[tuple[float, int]],
    samples: list[tuple[float, int, int]],
) -> tuple[np.ndarray, np.ndarray]:
    points = sorted(points)
    if not points or len(samples) < 2:
        raise ValueError("Missing progress or FDP samples")
    point_t = np.asarray([item[0] for item in points])
    point_k = np.asarray([item[1] for item in points], dtype=float)
    sample_t = np.asarray([item[0] for item in samples])
    host = np.asarray([item[1] for item in samples], dtype=float)
    media = np.asarray([item[2] for item in samples], dtype=float)
    keys = np.interp(sample_t, point_t, point_k, left=0, right=point_k[-1])
    host -= host[0]
    media -= media[0]
    valid = (host > 1024 * 1024) & (keys > 0)
    return (
        np.concatenate(([0.0], keys[valid] / 1_000_000)),
        np.concatenate(([1.0], media[valid] / host[valid])),
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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-tee", type=Path, required=True)
    parser.add_argument("--baseline-stats", type=Path, required=True)
    parser.add_argument("--model-initial-tee", type=Path, required=True)
    parser.add_argument("--model-resume-tees", type=Path, nargs="*", default=[])
    parser.add_argument("--model-stats", type=Path, nargs="+", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--phase1-keys", type=int, default=50_000_000)
    parser.add_argument("--phase2-keys", type=int, default=100_000_000)
    parser.add_argument("--step-keys", type=int, default=5_000_000)
    args = parser.parse_args()

    targets = (
        np.arange(
            0,
            args.phase1_keys + args.phase2_keys + 1,
            args.step_keys,
            dtype=float,
        )
        / 1_000_000
    )
    base_x, base_alwa = complete_alwa(args.baseline_tee, args.phase1_keys)
    model_x, model_alwa = segmented_alwa(
        args.model_initial_tee,
        args.model_resume_tees,
        args.phase1_keys,
        args.phase2_keys,
    )
    base_dx, base_dlwa = dlwa(
        progress(args.baseline_tee, args.phase1_keys),
        device_samples([args.baseline_stats]),
    )
    model_progress = progress(args.model_initial_tee, args.phase1_keys)
    for path in args.model_resume_tees:
        model_progress.extend(progress(path, args.phase1_keys, True))
    model_dx, model_dlwa = dlwa(
        model_progress,
        device_samples(args.model_stats),
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
            zip(
                targets,
                interpolate(model_x, model_alwa, targets),
                interpolate(base_x, base_alwa, targets),
                interpolate(model_dx, model_dlwa, targets),
                interpolate(base_dx, base_dlwa, targets),
            )
        )
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
