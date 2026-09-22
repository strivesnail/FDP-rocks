#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def plot_throughput(data: Path, output: Path) -> None:
    rows = read_csv(data)
    labels = [row["workload"] for row in rows]
    nofdp = np.array([float(row["rocksdb_nofdp_kops"]) for row in rows])
    baseline = np.array([float(row["rocksdb_fdp_kops"]) for row in rows])
    fdp_rocks = np.array([float(row["fdp_rocks_kops"]) for row in rows])
    x = np.arange(len(rows))
    width = 0.24

    fig, axis = plt.subplots(figsize=(5.2, 3.1))
    axis.bar(x - width, nofdp, width, label="RocksDB-NoFDP", color="#4c72b0")
    axis.bar(x, baseline, width, label="RocksDB-FDP", color="#d62728")
    axis.bar(x + width, fdp_rocks, width, label="FDP-Rocks", color="#2ca02c")
    axis.set_ylabel("Throughput (Kops/s)")
    axis.set_xticks(x, labels)
    axis.grid(axis="y", linestyle=":", color="#aaaaaa")
    axis.set_axisbelow(True)
    axis.spines["top"].set_visible(False)
    axis.spines["right"].set_visible(False)
    axis.legend(frameon=False)
    fig.tight_layout()
    fig.savefig(output.with_suffix(".pdf"))
    fig.savefig(output.with_suffix(".png"), dpi=200)
    plt.close(fig)


def plot_wa(data: Path, output: Path) -> None:
    rows = read_csv(data)
    x = np.array([float(row["key_write_count_millions"]) for row in rows])
    alwa_fdp = np.array([float(row["alwa_fdp_rocks"]) for row in rows])
    alwa_base = np.array([float(row["alwa_rocksdb_fdp"]) for row in rows])
    dlwa_fdp = np.array([float(row["dlwa_fdp_rocks"]) for row in rows])
    dlwa_base = np.array([float(row["dlwa_rocksdb_fdp"]) for row in rows])

    fig, left = plt.subplots(figsize=(5.2, 3.2))
    right = left.twinx()
    left.plot(x, alwa_fdp, "o-", ms=3, color="#2ca02c", label="ALWA of FDP-Rocks")
    left.plot(x, alwa_base, "o-", ms=3, color="#d62728", label="ALWA of RocksDB-FDP")
    right.plot(x, dlwa_base, "^-", ms=3, color="#e7a1a1", label="DLWA of RocksDB-FDP")
    right.plot(x, dlwa_fdp, "^-", ms=3, color="#9bc99b", label="DLWA of FDP-Rocks")
    left.set_xlabel("Key Write Count (millions)")
    left.set_ylabel("ALWA")
    right.set_ylabel("DLWA")
    left.set_xlim(0, 150)
    left.set_ylim(bottom=0)
    right.set_ylim(0, 2.1)
    left.grid(color="#bdbdbd", linewidth=0.5, alpha=0.65)
    left.set_axisbelow(True)
    handles_l, labels_l = left.get_legend_handles_labels()
    handles_r, labels_r = right.get_legend_handles_labels()
    left.legend(handles_l + handles_r, labels_l + labels_r, frameon=False, fontsize=8)
    fig.tight_layout()
    fig.savefig(output.with_suffix(".pdf"))
    fig.savefig(output.with_suffix(".png"), dpi=200)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("throughput", "wa"), required=True)
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if args.kind == "throughput":
        plot_throughput(args.data, args.output)
    else:
        plot_wa(args.data, args.output)


if __name__ == "__main__":
    main()
