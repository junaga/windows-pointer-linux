# windows-pointer-linux

the Windows 11 pointer algorithm for Hyprland. because apparently a cursor can
have platform exclusives.

This plugin takes the raw counts Hyprland already receives from a physical
mouse and produces the accelerated delta with Windows' fixed-point algorithm.
Touchpads and virtual pointers pass through unchanged. Each mouse gets its own
history and subpixel accumulator, including mice connected after startup.

v0.1 implements the published reverse engineering of the Windows 11 algorithm.
It is covered by deterministic tests, but it has not yet been compared against
a Windows oracle, so "bit-perfect" would be lying with confidence.

## build it

You need CMake 3.25+, a C++23 compiler, pkg-config, and development headers for
the exact Hyprland version currently running.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
hyprctl plugin load "$(pwd)/build/windows-pointer-linux.so"
```

Hyprland plugins do not have a stable C++ ABI. The plugin checks the full ABI
string and refuses to load when it does not match. After a Hyprland update,
rebuild it. This is annoying, but much less annoying than crashing the session
with yesterday's vtable.

Unload the plugin before rebuilding the same `.so` in place:

```sh
hyprctl plugin unload "$(pwd)/build/windows-pointer-linux.so"
```

`hyprpm.toml` is included for installation through hyprpm once the repository
has a public URL.

## configure it

Load the plugin before declaring its values:

```lua
hl.plugin.load("/absolute/path/to/windows-pointer-linux/build/windows-pointer-linux.so")

hl.config({
    plugin = {
        windows_pointer_linux = {
            pointer_speed = "10/20",
            enhance_pointer_precision = true,
        },
    },
})
```

Those are deliberately the two controls from Windows 11 Settings:

- `pointer_speed` accepts `"1/20"` through `"20/20"`; `"10/20"` is the default.
- `enhance_pointer_precision` enables or disables the Windows acceleration
  curve.

Do not enable Hyprland's `input.force_no_accel`: it tells Hyprland to ignore the
accelerated delta, including ours. Existing libinput sensitivity and profile
settings do not shape mouse motion while this plugin is active because the
plugin replaces that delta from the raw counts. Raw-input clients still receive
the honest unaccelerated delta. Touchpad settings continue to work normally.

There is no mouse DPI or polling-rate setting. DPI is already represented by
how many raw counts the mouse reports; polling rate is already represented by
how those counts are split into reports. Asking for either again would count
the same fact twice, which computers are extremely willing to do.

The current curve uses Windows' 100% display-scaling reference (96 DPI).
Per-monitor display scaling, rotated devices, and special pointer hardware are
future work.

## why this layer

- A [libinput custom profile][libinput-custom] is convenient and static, but it
  models output as a sampled function of libinput's calculated velocity. The
  Windows implementation has its own fixed-point curve, segment-transition
  smoothing, and subpixel state.
- [libinput-epp][libinput-epp] puts the reverse-engineered algorithm in the
  right library, but requires replacing or rebuilding system libinput.
- [libinput Lua plugins][libinput-lua] can edit raw evdev frames without a
  fork, but plugins are opt-in at the compositor and [Hyprland][hyprland-plugins]
  does not currently load them.
- A uinput daemon can work across compositors, at the cost of grabbing devices,
  creating virtual ones, permissions, and a service. an entire fake mouse to
  move the real mouse is funny exactly once.
- This Hyprland plugin is small and local. Its tax is rebuilding against
  Hyprland's unstable plugin ABI.

## sources

The EPP curve and fixed-point behavior are independently implemented from
[tehabstract's reverse-engineered libinput patch][libinput-epp], which documents
the corresponding `win32kbase` routines. The non-EPP 1–20 gains follow the
piecewise Windows sensitivity mapping documented by [Mark Cranness][markc].
Microsoft documents the public [1–20 setting and default value of 10][spi].

The project is BSD-3-Clause licensed. libinput and the reference patch are MIT
licensed.

[hyprland-plugins]: https://wiki.hypr.land/Plugins/Using-Plugins/
[libinput-custom]: https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html#the-custom-acceleration-profile
[libinput-epp]: https://gitlab.freedesktop.org/tehabstract/libinput-epp
[libinput-lua]: https://wayland.freedesktop.org/libinput/doc/latest/lua-plugins.html
[markc]: https://www.esreality.com/post/1846538/markc-windows-7-mouse-acceleration-fix/
[spi]: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-systemparametersinfow
