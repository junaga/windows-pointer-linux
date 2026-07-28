#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import pathlib


def read_metadata(path: pathlib.Path) -> dict[str, int]:
    values: dict[str, int] = {}
    for line_number, line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), 1
    ):
        key, separator, value = line.partition("=")
        if not separator:
            raise ValueError(f"{path}:{line_number}: expected key=value")
        values[key] = int(value)
    return values


def convert(capture: pathlib.Path, output: pathlib.Path) -> tuple[int, int, int]:
    metadata = read_metadata(capture / "metadata.txt")
    required_metadata = {
        "initial_cursor_x",
        "initial_cursor_y",
        "pointer_speed",
        "mouse_acceleration",
        "system_dpi",
    }
    missing = required_metadata - metadata.keys()
    if missing:
        raise ValueError(
            f"{capture / 'metadata.txt'}: missing {','.join(sorted(missing))}"
        )
    expected_settings = {
        "pointer_speed": 10,
        "mouse_threshold_1": 6,
        "mouse_threshold_2": 10,
        "mouse_acceleration": 1,
        "system_dpi": 96,
    }
    mismatched_settings = {
        key: (metadata.get(key), expected)
        for key, expected in expected_settings.items()
        if metadata.get(key) != expected
    }
    if mismatched_settings:
        details = ", ".join(
            f"{key}={actual} (expected {expected})"
            for key, (actual, expected) in mismatched_settings.items()
        )
        raise ValueError(f"Windows reference settings mismatch: {details}")

    with (capture / "raw.csv").open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required_columns = {
            "report",
            "time_ns",
            "flags",
            "raw_x",
            "raw_y",
            "cursor_x",
            "cursor_y",
        }
        if reader.fieldnames is None or not required_columns.issubset(
            reader.fieldnames
        ):
            raise ValueError(
                f"{capture / 'raw.csv'}: missing required columns"
            )
        rows = list(reader)

    with (capture / "pointer.csv").open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        required_columns = {
            "event",
            "time_ns",
            "message",
            "cursor_x",
            "cursor_y",
            "flags",
        }
        if reader.fieldnames is None or not required_columns.issubset(
            reader.fieldnames
        ):
            raise ValueError(
                f"{capture / 'pointer.csv'}: missing required columns"
            )
        pointer_rows = [
            row for row in reader if int(row["message"]) == 0x0200
        ]

    if len(pointer_rows) != len(rows):
        raise ValueError(
            f"Windows event mismatch: {len(rows)} raw reports but "
            f"{len(pointer_rows)} low-level mouse moves"
        )

    output.parent.mkdir(parents=True, exist_ok=True)
    previous_x = metadata["initial_cursor_x"]
    previous_y = metadata["initial_cursor_y"]
    cumulative_x = 0
    cumulative_y = 0
    with output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(
            (
                "report",
                "time_ns",
                "raw_x",
                "raw_y",
                "output_x",
                "output_y",
            )
        )
        for expected_report, (row, pointer_row) in enumerate(
            zip(rows, pointer_rows, strict=True)
        ):
            report = int(row["report"])
            if report != expected_report:
                raise ValueError(
                    f"{capture / 'raw.csv'}: expected report "
                    f"{expected_report}, got {report}"
                )
            flags = int(row["flags"])
            if flags & 0x01:
                raise ValueError(
                    f"report {report} is absolute input; relative input required"
                )
            cursor_x = int(pointer_row["cursor_x"])
            cursor_y = int(pointer_row["cursor_y"])
            output_x = cursor_x - previous_x
            output_y = cursor_y - previous_y
            writer.writerow(
                (
                    report,
                    int(row["time_ns"]),
                    int(row["raw_x"]),
                    int(row["raw_y"]),
                    output_x,
                    output_y,
                )
            )
            previous_x = cursor_x
            previous_y = cursor_y
            cumulative_x += output_x
            cumulative_y += output_y

    return len(rows), cumulative_x, cumulative_y


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("capture", type=pathlib.Path)
    parser.add_argument("output", type=pathlib.Path)
    arguments = parser.parse_args()
    count, cumulative_x, cumulative_y = convert(
        arguments.capture, arguments.output
    )
    print(
        f"converted {count} Windows reports; final position delta "
        f"({cumulative_x},{cumulative_y})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
