#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
import pathlib
import platform
import shutil
import subprocess
from typing import Any


REQUIRED_COMMANDS = (
    "cloud-localds",
    "qemu-system-x86_64",
    "qemu-img",
    "virsh",
    "virt-install",
    "swtpm",
    "tshark",
    "vncdo",
    "wimlib-imagex",
    "x86_64-w64-mingw32-g++",
    "xorriso",
)

LAB_ROOT = pathlib.Path(
    os.environ.get(
        "WINDOWS_POINTER_LAB_ROOT",
        "/usr/local/var/lib/windows-pointer-lab",
    )
)
PROVISIONED_DISKS = (
    LAB_ROOT / "vms/wplab-linux.qcow2",
    LAB_ROOT / "vms/wplab-windows.qcow2",
)


def read_text(path: str) -> str:
    try:
        return pathlib.Path(path).read_text(encoding="utf-8").strip()
    except (OSError, UnicodeError):
        return ""


def command_output(*command: str) -> str:
    try:
        return subprocess.run(
            command,
            check=False,
            capture_output=True,
            text=True,
        ).stdout.strip()
    except OSError:
        return ""


def cpu_flags() -> set[str]:
    for line in read_text("/proc/cpuinfo").splitlines():
        if line.startswith("flags"):
            _, _, values = line.partition(":")
            return set(values.split())
    return set()


def module_enabled(name: str) -> bool:
    config = read_text(f"/boot/config-{platform.release()}")
    return f"CONFIG_{name}=m" in config or f"CONFIG_{name}=y" in config


def disk_free(path: str) -> int:
    try:
        return shutil.disk_usage(path).free
    except OSError:
        return 0


def memory_available() -> int:
    for line in read_text("/proc/meminfo").splitlines():
        if line.startswith("MemAvailable:"):
            return int(line.split()[1]) * 1024
    return 0


def memory_total() -> int:
    for line in read_text("/proc/meminfo").splitlines():
        if line.startswith("MemTotal:"):
            return int(line.split()[1]) * 1024
    return 0


def collect() -> dict[str, Any]:
    flags = cpu_flags()
    commands = {name: shutil.which(name) for name in REQUIRED_COMMANDS}
    udcs = sorted(pathlib.Path("/sys/class/udc").glob("*"))
    bluetooth_adapters = sorted(
        path
        for path in pathlib.Path("/sys/class/bluetooth").glob("hci*")
        if path.name.removeprefix("hci").isdigit()
    )
    usb = command_output("lsusb")
    guests_provisioned = all(path.exists() for path in PROVISIONED_DISKS)
    required_free_disk = (1 if guests_provisioned else 45) * 1024**3

    checks = {
        "vmx": "vmx" in flags,
        "kvm_device": pathlib.Path("/dev/kvm").exists(),
        "kvm_access": os.access("/dev/kvm", os.R_OK | os.W_OK),
        "two_dummy_udcs": (
            len(udcs) >= 2 or module_enabled("USB_DUMMY_HCD")
        ),
        "gadget_modules": all(
            module_enabled(name)
            for name in ("USB_DUMMY_HCD", "USB_LIBCOMPOSITE", "USB_F_HID")
        ),
        "commands": all(commands.values()),
        "winrm_module": bool(
            command_output(
                "python3",
                "-c",
                "import winrm; print('ok')",
            )
        ),
        "memory": memory_total() >= 15 * 1024**3,
        "disk": disk_free("/usr/local") >= required_free_disk,
    }

    return {
        "checks": checks,
        "ready": {
            "digital_vm": all(
                checks[name]
                for name in (
                    "vmx",
                    "kvm_device",
                    "kvm_access",
                    "two_dummy_udcs",
                    "gadget_modules",
                    "commands",
                    "winrm_module",
                    "memory",
                    "disk",
                )
            ),
            "dedicated_bluetooth_usb": len(bluetooth_adapters) >= 2,
        },
        "host": {
            "kernel": platform.release(),
            "memory_total_bytes": memory_total(),
            "memory_available_bytes": memory_available(),
            "usr_local_free_bytes": disk_free("/usr/local"),
            "required_free_disk_bytes": required_free_disk,
            "guests_provisioned": guests_provisioned,
            "dummy_udcs": [path.name for path in udcs],
            "bluetooth_adapters": [path.name for path in bluetooth_adapters],
            "commands": commands,
            "usb": usb.splitlines(),
        },
    }


def human(result: dict[str, Any]) -> None:
    labels = {
        "vmx": "Intel VMX exposed",
        "kvm_device": "/dev/kvm exists",
        "kvm_access": "/dev/kvm is accessible",
        "two_dummy_udcs": "two dummy USB controllers can be created",
        "gadget_modules": "USB gadget kernel support exists",
        "commands": "host commands are installed",
        "winrm_module": "Python WinRM support is installed",
        "memory": "at least 15 GiB physical memory is installed",
        "disk": (
            "at least 1 GiB is free for the provisioned lab"
            if result["host"]["guests_provisioned"]
            else "at least 45 GiB is free to provision the lab"
        ),
    }
    for name, passed in result["checks"].items():
        print(f"{'ok' if passed else 'BLOCKED':7} {labels[name]}")
    print()
    print(
        "digital VM test:",
        "ready" if result["ready"]["digital_vm"] else "blocked",
    )
    print(
        "dedicated Bluetooth hardware test:",
        (
            "ready"
            if result["ready"]["dedicated_bluetooth_usb"]
            else "needs a second USB Bluetooth adapter"
        ),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--json", action="store_true")
    arguments = parser.parse_args()
    result = collect()
    if arguments.json:
        print(json.dumps(result, indent=2))
    else:
        human(result)
    return 0 if result["ready"]["digital_vm"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
