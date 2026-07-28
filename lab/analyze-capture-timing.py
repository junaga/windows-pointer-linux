#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import pathlib
import statistics


def read_column(path: pathlib.Path, column: str) -> list[int]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = (
            line
            for line in stream
            if line.strip() and not line.startswith("#")
        )
        reader = csv.DictReader(rows)
        if reader.fieldnames is None or column not in reader.fieldnames:
            raise ValueError(f"{path}: missing {column}")
        return [int(row[column]) for row in reader]


def intervals(values: list[int]) -> list[int]:
    return [right - left for left, right in zip(values, values[1:])]


def percentile(values: list[int], percent: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    position = (len(ordered) - 1) * percent
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def describe(name: str, values: list[int], reference: list[int]) -> None:
    sample_intervals = intervals(values)
    reference_intervals = intervals(reference)
    if len(sample_intervals) != len(reference_intervals):
        print(
            f"{name}: unavailable ({len(values)} samples; "
            f"reference has {len(reference)})"
        )
        return
    errors = [
        sample - expected
        for sample, expected in zip(
            sample_intervals, reference_intervals, strict=True
        )
    ]
    absolute_errors = [abs(error) for error in errors]
    print(
        f"{name}: intervals={len(sample_intervals)}, "
        f"median={statistics.median(sample_intervals) / 1e6:.3f} ms, "
        f"p95_abs_error={percentile(absolute_errors, 0.95) / 1e6:.3f} ms, "
        f"max_abs_error={max(absolute_errors, default=0) / 1e6:.3f} ms"
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", type=pathlib.Path)
    parser.add_argument("windows", type=pathlib.Path)
    parser.add_argument("linux", type=pathlib.Path)
    arguments = parser.parse_args()

    scheduled = read_column(arguments.host, "scheduled_ns")
    windows_host = read_column(arguments.host, "device_0_start_ns")
    linux_host = read_column(arguments.host, "device_1_start_ns")
    windows_guest = read_column(arguments.windows, "time_ns")
    linux_guest = read_column(arguments.linux, "time_ns")

    describe("host→Windows gadget writes", windows_host, scheduled)
    describe("host→Linux gadget writes", linux_host, scheduled)
    describe("Windows guest receipt", windows_guest, scheduled)
    describe("Linux guest receipt", linux_guest, scheduled)

    write_skew = [
        abs(windows - linux)
        for windows, linux in zip(windows_host, linux_host, strict=True)
    ]
    print(
        "simultaneous host-write skew: "
        f"median={statistics.median(write_skew) / 1e6:.3f} ms, "
        f"p95={percentile(write_skew, 0.95) / 1e6:.3f} ms, "
        f"max={max(write_skew, default=0) / 1e6:.3f} ms"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
