#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def rows(path: Path) -> tuple[list[str], list[list[str]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.reader(handle)
        return next(reader), list(reader)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("--tolerance", type=float, default=1e-9)
    args = parser.parse_args()

    expected_header, expected_rows = rows(args.expected)
    actual_header, actual_rows = rows(args.actual)
    if expected_header != actual_header:
        raise SystemExit("CSV headers differ")
    if len(expected_rows) != len(actual_rows):
        raise SystemExit("CSV row counts differ")
    for row_index, (expected, actual) in enumerate(
        zip(expected_rows, actual_rows), start=2
    ):
        if len(expected) != len(actual):
            raise SystemExit(f"CSV column count differs at row {row_index}")
        for column, (left, right) in enumerate(zip(expected, actual), start=1):
            try:
                difference = abs(float(left) - float(right))
            except ValueError:
                if left != right:
                    raise SystemExit(
                        f"CSV text differs at row {row_index}, column {column}"
                    )
            else:
                if difference > args.tolerance:
                    raise SystemExit(
                        f"CSV value differs at row {row_index}, column {column}: "
                        f"{left} vs {right}"
                    )
    print(f"CSV comparison passed: {args.actual}")


if __name__ == "__main__":
    main()
