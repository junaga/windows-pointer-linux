#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import os
import pathlib
import struct
import threading
import time
from dataclasses import dataclass
from typing import TextIO


@dataclass(frozen=True)
class Report:
    x: int
    y: int
    buttons: int = 0

    def encode(self) -> bytes:
        return struct.pack("<Bhh", self.buttons, self.x, self.y)


def parse_number(value: str, name: str, minimum: int, maximum: int) -> int:
    try:
        number = int(value, 10)
    except ValueError as error:
        raise ValueError(f"{name} must be an integer") from error
    if not minimum <= number <= maximum:
        raise ValueError(f"{name} must be between {minimum} and {maximum}")
    return number


def read_trace(stream: TextIO) -> list[Report]:
    reports: list[Report] = []
    for line_number, raw_line in enumerate(stream, 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        fields = [field.strip() for field in line.split(",")]
        if len(fields) not in (2, 3):
            raise ValueError(
                f"line {line_number}: expected x,y or x,y,buttons"
            )
        reports.append(
            Report(
                parse_number(fields[0], "x", -32768, 32767),
                parse_number(fields[1], "y", -32768, 32767),
                parse_number(fields[2], "buttons", 0, 255)
                if len(fields) == 3
                else 0,
            )
        )
    if not reports:
        raise ValueError("the trace contains no reports")
    return reports


def devices_from_state(path: pathlib.Path) -> list[pathlib.Path]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value
    devices = [
        pathlib.Path(values[name])
        for name in ("WINDOWS_HIDG", "LINUX_HIDG")
        if name in values
    ]
    if len(devices) != 2:
        raise ValueError(f"{path} does not name both HID gadget endpoints")
    return devices


class Fanout:
    def __init__(self, devices: list[pathlib.Path]) -> None:
        self._paths = devices
        self._files = [
            os.open(path, os.O_WRONLY | os.O_CLOEXEC) for path in devices
        ]
        self._start = threading.Barrier(len(devices) + 1)
        self._finish = threading.Barrier(len(devices) + 1)
        self._report = b""
        self._stop = False
        self._times = [(0, 0) for _ in devices]
        self._errors: list[BaseException] = []
        self._threads = [
            threading.Thread(
                target=self._worker,
                args=(index,),
                name=f"hid-writer-{index}",
                daemon=True,
            )
            for index in range(len(devices))
        ]
        for thread in self._threads:
            thread.start()

    def close(self) -> None:
        self._stop = True
        try:
            self._start.wait(timeout=2)
        except threading.BrokenBarrierError:
            pass
        for thread in self._threads:
            thread.join(timeout=2)
        for descriptor in self._files:
            os.close(descriptor)

    def write(self, report: Report) -> list[tuple[int, int]]:
        if self._errors:
            raise RuntimeError("HID writer failed") from self._errors[0]
        self._report = report.encode()
        try:
            self._start.wait()
            self._finish.wait()
        except threading.BrokenBarrierError as error:
            if self._errors:
                raise RuntimeError("HID writer failed") from self._errors[0]
            raise RuntimeError("HID writer synchronization failed") from error
        if self._errors:
            raise RuntimeError("HID writer failed") from self._errors[0]
        return list(self._times)

    def _worker(self, index: int) -> None:
        try:
            while True:
                self._start.wait()
                if self._stop:
                    return
                started = time.monotonic_ns()
                written = os.write(self._files[index], self._report)
                ended = time.monotonic_ns()
                if written != len(self._report):
                    raise OSError(
                        f"short HID write to {self._paths[index]}: {written}"
                    )
                self._times[index] = (started, ended)
                self._finish.wait()
        except BaseException as error:
            self._errors.append(error)
            self._start.abort()
            self._finish.abort()


def wait_until(deadline_ns: int) -> None:
    while True:
        remaining = deadline_ns - time.monotonic_ns()
        if remaining <= 0:
            return
        if remaining > 200_000:
            time.sleep((remaining - 100_000) / 1_000_000_000)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=pathlib.Path)
    parser.add_argument("--device", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--from-state", type=pathlib.Path)
    parser.add_argument("--interval-ms", type=float, default=20.0)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    arguments = parser.parse_args()

    devices = list(arguments.device)
    if arguments.from_state:
        devices.extend(devices_from_state(arguments.from_state))
    if len(devices) < 1:
        parser.error("provide --device or --from-state")
    if arguments.interval_ms <= 0:
        parser.error("--interval-ms must be positive")

    with arguments.trace.open(encoding="utf-8") as stream:
        reports = read_trace(stream)

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    interval_ns = round(arguments.interval_ms * 1_000_000)
    columns = [
        "report",
        "scheduled_ns",
        "raw_x",
        "raw_y",
        "buttons",
    ]
    for index in range(len(devices)):
        columns.extend((f"device_{index}_start_ns", f"device_{index}_end_ns"))

    fanout = Fanout(devices)
    try:
        origin = time.monotonic_ns() + 100_000_000
        with arguments.output.open("w", newline="", encoding="utf-8") as output:
            writer = csv.writer(output)
            writer.writerow(columns)
            for index, report in enumerate(reports):
                scheduled = origin + index * interval_ns
                wait_until(scheduled)
                times = fanout.write(report)
                row: list[int] = [
                    index,
                    scheduled,
                    report.x,
                    report.y,
                    report.buttons,
                ]
                for started, ended in times:
                    row.extend((started, ended))
                writer.writerow(row)
    finally:
        fanout.close()

    print(
        f"replayed {len(reports)} reports to {len(devices)} devices; "
        f"host timing: {arguments.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
