#!/usr/bin/env python3
from __future__ import annotations

import csv
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data" / "precollected"


def load(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise ValueError(f"No rows in {path}")
    return rows


def finite_columns(rows: list[dict[str, str]], columns: tuple[str, ...]) -> None:
    for row in rows:
        for column in columns:
            value = float(row[column])
            if not math.isfinite(value):
                raise ValueError(f"Non-finite {column}: {value}")


def main() -> None:
    throughput = load(DATA / "figure6a.csv")
    if len(throughput) != 4:
        raise ValueError("Figure 6(a) must contain four workloads")
    finite_columns(
        throughput,
        ("rocksdb_nofdp_kops", "rocksdb_fdp_kops", "fdp_rocks_kops"),
    )
    expected = [
        ("Uniform", 31.5, 34.1, 44.8),
        ("Zipf_s_0.5", 33.3, 35.5, 46.1),
        ("Zipf_s_1.5", 48.8, 51.9, 63.7),
        ("Zipf_s_3", 66.6, 70.8, 78.9),
    ]
    observed = [
        (
            row["workload"],
            float(row["rocksdb_nofdp_kops"]),
            float(row["rocksdb_fdp_kops"]),
            float(row["fdp_rocks_kops"]),
        )
        for row in throughput
    ]
    if observed != expected:
        raise ValueError("Figure 6(a) values differ from the published chart")

    for name in ("figure6b.csv", "figure6c.csv"):
        rows = load(DATA / name)
        finite_columns(
            rows,
            (
                "key_write_count_millions",
                "alwa_fdp_rocks",
                "alwa_rocksdb_fdp",
                "dlwa_fdp_rocks",
                "dlwa_rocksdb_fdp",
            ),
        )
        if float(rows[-1]["key_write_count_millions"]) != 150.0:
            raise ValueError(f"{name} does not end at 150 million writes")

    print("Pre-collected data validation passed.")


if __name__ == "__main__":
    main()
