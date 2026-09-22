#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np


WORKLOADS = {
    "uniform": None,
    "zipf_s0.5": 0.5,
    "zipf_s1.5": 1.5,
    "zipf_s3": 3.0,
}


def checksum(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_phase1(
    path: Path, count: int, keyspace: int, rng: np.random.Generator
) -> None:
    remaining = count
    with path.open("wb") as handle:
        while remaining:
            keys = np.arange(1, keyspace + 1, dtype="<u8")
            rng.shuffle(keys)
            chunk = keys[:remaining]
            handle.write(chunk.tobytes())
            remaining -= len(chunk)


def write_phase2(
    path: Path,
    count: int,
    keyspace: int,
    exponent: float | None,
    seed: int,
) -> None:
    rng = np.random.default_rng(seed)
    mapping = np.arange(1, keyspace + 1, dtype="<u8")
    rng.shuffle(mapping)
    weights = None
    if exponent is not None:
        ranks = np.arange(1, keyspace + 1, dtype=np.float64)
        weights = np.power(ranks, -exponent)
        weights /= weights.sum()

    with path.open("wb") as handle:
        remaining = count
        while remaining:
            size = min(remaining, 1_000_000)
            if weights is None:
                ranks = rng.integers(0, keyspace, size=size)
            else:
                ranks = rng.choice(keyspace, size=size, replace=True, p=weights)
            keys = mapping[ranks].astype("<u8", copy=False)
            rng.shuffle(keys)
            handle.write(keys.tobytes())
            remaining -= size


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", type=Path, default=Path("data/examples"))
    parser.add_argument("--phase1-keys", type=int, default=10_000)
    parser.add_argument("--phase2-keys", type=int, default=20_000)
    parser.add_argument("--keyspace", type=int, default=10_000)
    parser.add_argument("--seed", type=int, default=42)
    args = parser.parse_args()

    if min(args.phase1_keys, args.phase2_keys, args.keyspace) <= 0:
        parser.error("Key counts and keyspace must be positive.")
    args.output_dir.mkdir(parents=True, exist_ok=True)

    phase1 = args.output_dir / "phase1_uniform.bin"
    write_phase1(
        phase1,
        args.phase1_keys,
        args.keyspace,
        np.random.default_rng(args.seed),
    )
    manifest: dict[str, object] = {
        "format": "little-endian uint64 keys",
        "seed": args.seed,
        "keyspace": args.keyspace,
        "phase1_keys": args.phase1_keys,
        "phase2_keys": args.phase2_keys,
        "files": {
            phase1.name: {
                "bytes": phase1.stat().st_size,
                "sha256": checksum(phase1),
            }
        },
    }

    files = manifest["files"]
    assert isinstance(files, dict)
    for index, (name, exponent) in enumerate(WORKLOADS.items(), start=1):
        output = args.output_dir / f"phase2_{name}.bin"
        write_phase2(
            output,
            args.phase2_keys,
            args.keyspace,
            exponent,
            args.seed + index,
        )
        files[output.name] = {
            "bytes": output.stat().st_size,
            "sha256": checksum(output),
        }

    manifest_path = args.output_dir / "manifest.json"
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"Wrote example workloads and {manifest_path}")


if __name__ == "__main__":
    main()
