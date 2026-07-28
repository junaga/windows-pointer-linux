# Windows/Linux pointer lab

The safe default is a fully automated, simultaneous Windows/Linux test. It
does not detach the host keyboard, mouse, webcam, Bluetooth radio, or USB
controller.

## everyday commands

```sh
# Build, start both VMs, run the exact comparison, and clean up test devices.
lab/pointer-lab.sh test

# Use a particular input trace.
lab/pointer-lab.sh test lab/traces/full-scenarios.csv

# Manage or inspect the friendly VMs.
lab/pointer-lab.sh start
lab/pointer-lab.sh status
lab/pointer-lab.sh screenshot windows
lab/pointer-lab.sh screenshot linux
lab/pointer-lab.sh stop
```

`test` is unattended. It builds current code, boots and repairs the existing
guests, synchronizes the Linux plugin and Windows collector, and creates two
kernel-backed synthetic USB HID mice. The same report is written to both
devices behind a per-report barrier. The test then:

1. captures Windows Raw Input and desktop pointer events;
2. captures Linux/Hyprland raw and transformed reports;
3. requires every raw and accelerated delta to match exactly;
4. reports guest timing and simultaneous host-write skew;
5. detaches and removes both synthetic devices, including after failures.

Timestamped artifacts are written below `build/lab/dual-vm-*`.

## friendly guests

- `wplab-linux` is Debian 14 with an auto-started Hyprland session, a flat
  libinput profile, SSH, and the current plugin.
- `wplab-windows` is a disposable Windows 11 VM with persistent auto-login,
  default Windows pointer settings (10/20 with EPP enabled), WinRM on the
  libvirt-private network, and a visible Pointer Lab console for Raw Input.
- Both desktops use VNC bound to localhost. Scripts discover the assigned VNC
  port dynamically; no `5900`/`5901` ordering is assumed.
- Windows binaries are synchronized over the private VM network instead of
  being typed into a console or copied in tiny WinRM chunks.

The visible Windows console is deliberate. Windows does not deliver the same
desktop pointer stream to hidden service or scheduler sessions. The host opens
a fresh interactive PowerShell through the VM's virtual keyboard before each
capture; no physical keyboard is involved.

## initial provisioning

Check the Debian host:

```sh
python3 lab/preflight.py
```

The dependencies are ordinary Debian packages except for the Windows ISO
itself. The preflight checks KVM, libvirt/QEMU, configfs HID support, MinGW,
WinRM support, VNC automation, capture tools, RAM, and disk space.

Provision the Linux guest from the verified Debian cloud image:

```sh
lab/vm-lab.sh provision-linux
lab/vm-lab.sh wait-linux
```

Provision the Windows guest from a genuine Windows 11 ISO:

```sh
lab/vm-lab.sh provision-windows /absolute/path/to/windows-11.iso
lab/vm-lab.sh wait-windows
```

After provisioning, use only the everyday `pointer-lab.sh` commands.

## Bluetooth hardware tests

Real Bluetooth is optional and intentionally separate from pointer-algorithm
conformance.

```sh
sudo lab/bluetooth-radio.sh status
sudo lab/bluetooth-radio.sh attach-windows
sudo lab/bluetooth-radio.sh attach-linux
sudo lab/bluetooth-radio.sh detach
```

This path passes through **one dedicated USB Bluetooth adapter**, not a PCI
USB controller. It refuses any adapter carrying a connected host device. On
the current host, the built-in AX210 is marked `HOST-IN-USE` because it carries
the Pebble, so an additional USB Bluetooth dongle is required.

A dedicated dongle preserves the host keyboard, mouse, webcam, and built-in
Bluetooth. It covers the guest Bluetooth/HID stack and real radio transport.
It does not claim to test the guest against the motherboard's physical xHCI
driver; that distinction is not relevant to pointer acceleration and is not
worth taking over every host USB device.

Physical sensor movement cannot be automated without a mechanical actuator.
When nobody is present, run only the synthetic conformance test.

## unsafe research-only path

`lab/unsafe/full-xhci-controller.sh` is retained only to reproduce low-level
controller experiments. It refuses attachment unless an explicit expert
override is provided because it disconnects every device on the motherboard
controller. It is never called by preflight, `start`, `test`, `status`, or
`stop`.
