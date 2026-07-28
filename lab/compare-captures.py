#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import pathlib
from dataclasses import dataclass


@dataclass(frozen=True)
class Sample:
    report: int
    raw_x: int
    raw_y: int
    output_x: int
    output_y: int


def read_capture(path: pathlib.Path) -> list[Sample]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = (
            line
            for line in stream
            if line.strip() and not line.startswith("#")
        )
        reader = csv.DictReader(rows)
        required = {"report", "raw_x", "raw_y", "output_x", "output_y"}
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            raise ValueError(
                f"{path}: expected columns {','.join(sorted(required))}"
            )
        samples = [
            Sample(
                report=int(row["report"]),
                raw_x=int(row["raw_x"]),
                raw_y=int(row["raw_y"]),
                output_x=int(row["output_x"]),
                output_y=int(row["output_y"]),
            )
            for row in reader
        ]
    for expected, sample in enumerate(samples):
        if sample.report != expected:
            raise ValueError(
                f"{path}: expected report {expected}, got {sample.report}"
            )
    return samples


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("left", type=pathlib.Path)
    parser.add_argument("right", type=pathlib.Path)
    arguments = parser.parse_args()

    left = read_capture(arguments.left)
    right = read_capture(arguments.right)
    if len(left) != len(right):
        print(
            f"report-count mismatch: {arguments.left} has {len(left)}, "
            f"{arguments.right} has {len(right)}"
        )
        return 1

    left_position = [0, 0]
    right_position = [0, 0]
    mismatches = 0
    for left_sample, right_sample in zip(left, right, strict=True):
        left_position[0] += left_sample.output_x
        left_position[1] += left_sample.output_y
        right_position[0] += right_sample.output_x
        right_position[1] += right_sample.output_y
        if left_sample != right_sample:
            if mismatches < 10:
                print(
                    f"report {left_sample.report}: "
                    f"left raw=({left_sample.raw_x},{left_sample.raw_y}) "
                    f"out=({left_sample.output_x},{left_sample.output_y}); "
                    f"right raw=({right_sample.raw_x},{right_sample.raw_y}) "
                    f"out=({right_sample.output_x},{right_sample.output_y})"
                )
            mismatches += 1

    if mismatches:
        print(
            f"{mismatches} mismatches; final positions "
            f"left=({left_position[0]},{left_position[1]}) "
            f"right=({right_position[0]},{right_position[1]})"
        )
        return 1

    print(
        f"exact match: {len(left)} reports, final position "
        f"({left_position[0]},{left_position[1]})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
