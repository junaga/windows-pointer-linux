#!/usr/bin/env python3
"""Correlate a physical Linux mouse capture across Bluetooth and input layers."""

from __future__ import annotations

import argparse
import csv
import re
import statistics
from pathlib import Path


def percentile(values: list[float], percentage: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return float("nan")
    rank = (len(ordered) - 1) * percentage / 100
    lower = int(rank)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = rank - lower
    return ordered[lower] + (ordered[upper] - ordered[lower]) * fraction


def parse_plugin(path: Path) -> tuple[list[float], list[tuple[int, int]]]:
    timestamps: list[float] = []
    samples: list[tuple[int, int]] = []
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            timestamps.append(int(row["time_ns"]) / 1_000_000_000)
            samples.append((int(row["raw_x"]), int(row["raw_y"])))
    return timestamps, samples


def parse_evtest(path: Path) -> tuple[list[float], list[tuple[int, int]]]:
    timestamps: list[float] = []
    samples: list[tuple[int, int]] = []
    dx = dy = 0
    moved = False
    timestamp = 0.0
    relative = re.compile(
        r"Event: time ([0-9.]+), type \d+ \(EV_REL\), "
        r"code \d+ \((REL_X|REL_Y)\), value (-?\d+)"
    )
    sync = re.compile(r"Event: time ([0-9.]+), .* SYN_REPORT")
    for line in path.read_text(errors="replace").splitlines():
        match = relative.search(line)
        if match:
            timestamp = float(match.group(1))
            code, value = match.group(2), int(match.group(3))
            if code == "REL_X":
                dx += value
            else:
                dy += value
            moved = True
        elif sync.search(line) and moved:
            timestamps.append(timestamp)
            samples.append((dx, dy))
            dx = dy = 0
            moved = False
    return timestamps, samples


def parse_libinput(path: Path) -> tuple[list[float], list[tuple[int, int]]]:
    timestamps: list[float] = []
    samples: list[tuple[int, int]] = []
    number = r"[+-]?[0-9.]+"
    motion = re.compile(
        rf"\+\s*([0-9.]+)s\s+({number})/\s*({number})\s+"
        rf"\(\s*({number})/\s*({number})\)"
    )
    for line in path.read_text(errors="replace").splitlines():
        match = motion.search(line)
        if not match:
            continue
        timestamps.append(float(match.group(1)))
        samples.append(
            (round(float(match.group(4))), round(float(match.group(5))))
        )
    return timestamps, samples


def parse_bluetooth(path: Path) -> list[float]:
    timestamps: list[float] = []
    timestamp: float | None = None
    acl = re.compile(r"^\s*> ACL Data.* ([0-9.]+)$")
    for line in path.read_text(errors="replace").splitlines():
        match = acl.search(line)
        if match:
            timestamp = float(match.group(1))
        elif "Handle: 0x0028" in line and timestamp is not None:
            timestamps.append(timestamp)
    return timestamps


def intervals_ms(timestamps: list[float]) -> list[float]:
    return [
        (current - previous) * 1000
        for previous, current in zip(timestamps, timestamps[1:])
        if current >= previous
    ]


def describe(name: str, timestamps: list[float]) -> str:
    intervals = intervals_ms(timestamps)
    if not intervals:
        return f"{name}: {len(timestamps)} samples (no intervals)"
    active = [value for value in intervals if value <= 20]
    burst = [value for value in active if value <= 0.2]
    on_cadence = [value for value in active if 6 <= value <= 10]
    late = [value for value in active if 10 < value <= 20]
    burst_after_late = sum(
        current <= 0.2 and previous > 10
        for previous, current in zip(intervals, intervals[1:])
    )
    return (
        f"{name}: {len(timestamps)} samples\n"
        f"  all gaps: median={statistics.median(intervals):.3f} ms, "
        f"p95={percentile(intervals, 95):.3f} ms, max={max(intervals):.3f} ms\n"
        f"  active gaps (<=20 ms): {len(active)}, "
        f"median={statistics.median(active):.3f} ms, "
        f"p95={percentile(active, 95):.3f} ms\n"
        f"  burst <=0.2 ms: {len(burst)} ({100 * len(burst) / len(active):.1f}%), "
        f"6-10 ms: {len(on_cadence)}, 10-20 ms: {len(late)}, "
        f"late-then-burst pairs: {burst_after_late}"
    )


def align(
    reference_name: str,
    reference: list[tuple[int, int]],
    candidate_name: str,
    candidate: list[tuple[int, int]],
) -> str:
    if not reference or not candidate:
        return (
            f"{reference_name} vs {candidate_name}: cannot align "
            f"({len(reference)}/{len(candidate)} samples)"
        )
    best: tuple[tuple[int, int, int], int, int, int] | None = None
    for reference_offset in range(min(32, len(reference))):
        for candidate_offset in range(min(32, len(candidate))):
            count = min(
                len(reference) - reference_offset,
                len(candidate) - candidate_offset,
            )
            mismatches = sum(
                reference[reference_offset + index]
                != candidate[candidate_offset + index]
                for index in range(count)
            )
            score = (mismatches, -(count), reference_offset + candidate_offset)
            if best is None or score < best[0]:
                best = (
                    score,
                    count,
                    reference_offset,
                    candidate_offset,
                )
    assert best is not None
    score, count, reference_offset, candidate_offset = best
    mismatches = score[0]
    return (
        f"{reference_name}[{reference_offset}:] vs "
        f"{candidate_name}[{candidate_offset}:]: "
        f"{count} compared, {mismatches} mismatches, "
        f"tails={len(reference) - reference_offset - count}/"
        f"{len(candidate) - candidate_offset - count}"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=Path)
    args = parser.parse_args()
    capture = args.capture

    plugin_times, plugin = parse_plugin(capture / "plugin.csv")
    evtest_times, evtest = parse_evtest(capture / "evtest.txt")
    libinput_times, libinput = parse_libinput(capture / "libinput.txt")
    bluetooth_times = parse_bluetooth(capture / "bluetooth.txt")

    print(describe("Bluetooth ATT notifications", bluetooth_times))
    print(describe("evdev motion frames", evtest_times))
    print(describe("libinput unaccelerated motion", libinput_times))
    print(describe("Hyprland plugin reports", plugin_times))
    print()
    print(align("plugin", plugin, "evdev", evtest))
    print(align("evdev", evtest, "libinput", libinput))


if __name__ == "__main__":
    main()
