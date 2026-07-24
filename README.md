# windows-pointer-linux

Windows 11 desktop pointer motion for Hyprland. because apparently a cursor
can have platform exclusives.

The plugin takes the raw integer counts Hyprland already receives from a
physical mouse and replaces libinput's accelerated cursor delta with Windows'
fixed-point algorithm. It implements both public Windows 11 controls:

- pointer speed from `"1/20"` through `"20/20"`;
- Enhance pointer precision on or off.

There is no mouse-DPI or polling-rate field. Hardware DPI already changes the
counts a mouse reports, and polling behavior already changes how those counts
are divided into reports. Supplying them again would be extremely configurable
double counting.

## what is actually finished

Version 1.0 implements the default curves audited in two shipping
`win32kbase.sys` builds, the complete 1–20 non-EPP mapping, signed subpixel
remainders, upward segment smoothing, session-wide state shared by physical
mice, and per-monitor display scaling.

The engine is checked against an independent straight-line model across all
settings, five display DPI values, thousands of reports per combination, and
signed device-coordinate limits. It also has scenario tests, sanitizers,
fuzzing, a Windows observation tool, and a live synthetic mouse test through
libinput and a running Hyprland session. The boring details live in
[testing](docs/testing.md); the less boring reverse engineering lives in
[the Windows audit](docs/research/windows-11.md).

This is still an implementation of private Windows behavior, not a Microsoft
API contract. A future Windows update can change it. The hashes in the audit
make that a testable event instead of a spiritual disagreement.

## install

You need CMake 3.25+, a C++23 compiler, pkg-config, and development headers for
the exact Hyprland build you are running.

### local install

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
cmake --install build/release --prefix "$HOME/.local"
```

Then load the absolute path. Hyprland requires it:

```lua
local plugin = os.getenv("HOME")
    .. "/.local/lib/hyprland/plugins/windows-pointer-linux.so"

hl.plugin.load(plugin)
```

Hyprland plugins use an unstable C++ ABI. The plugin verifies the complete ABI
hash and refuses to load against a different compositor build. Rebuild and
reinstall it after a Hyprland update. annoying, but preferable to debugging a
stale vtable with your cursor.

Unload a manually loaded plugin before overwriting the same file:

```sh
hyprctl plugin unload \
  "$HOME/.local/lib/hyprland/plugins/windows-pointer-linux.so"
```

### hyprpm

The repository includes `hyprpm.toml`, so after this repository has a public
Git URL it can be installed normally:

```sh
hyprpm add https://github.com/OWNER/windows-pointer-linux
hyprpm enable windows-pointer-linux
hyprpm reload
```

Replace `OWNER`; no, GitHub has not agreed to infer it from the project name.

## configure

Load the plugin before declaring its values:

```lua
hl.config({
    plugin = {
        windows_pointer_linux = {
            pointer_speed = "10/20",
            enhance_pointer_precision = true,
        },
    },
})
```

Those are Windows 11's defaults. Configuration reloads preserve the motion
accumulators, matching Windows. Invalid speed strings are rejected by the
plugin and reported as a Hyprland notification.

Leave `input.force_no_accel` disabled: that setting explicitly tells Hyprland
to consume raw motion instead of the accelerated delta, including ours.
Libinput `sensitivity` and `accel_profile` no longer shape the normal cursor
while the plugin is active because their output is replaced.

Touchpads, virtual pointers, absolute devices, and fractional raw coordinates
created by arbitrary device rotation pass through unchanged. Quarter-turn
rotations normally remain integral. Applications that explicitly request
unaccelerated relative motion still get it; the desktop cursor changes, a
game's raw-input path does not.

## inspect it

```sh
hyprctl windows-pointer-linux
hyprctl -j windows-pointer-linux
hyprctl windows-pointer-linux reset
```

The status includes active settings, pass-through reasons, per-device report
counts, and the last raw/output motion. `reset` clears diagnostics and the
session pointer state. It exists for testing and for the ancient repair ritual
of turning a thing off and on without actually unloading it.

## scope and tradeoffs

The [architecture](docs/architecture.md) explains where the hook sits, what
state it owns, and why raw-input clients are untouched. The
[alternatives](docs/alternatives.md) compares the Hyprland custom-curve
generator, `libinput-epp`, libinput Lua plugins, uinput daemons, Xorg settings,
YeetMouse/LeetMouse/maccel, application acceleration, and Raw Accel.

The short version: a Hyprland plugin is the smallest layer that exposes the
required raw reports and cursor destination without replacing libinput,
installing a kernel module, or inventing a permanent fake mouse. Its price is
Hyprland-only support and a rebuild whenever the compositor ABI changes.

## develop

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

See [contributing](CONTRIBUTING.md) before changing arithmetic and
[testing](docs/testing.md) for the replay tool, Windows oracle, live compositor
test, fuzz target, sanitizers, and benchmark.

The project is BSD-3-Clause licensed. No Microsoft binary, PDB, or source code
is distributed here.

## primary references

- Microsoft documents the public
  [`SystemParametersInfo` mouse settings][spi].
- Libinput documents
  [raw unaccelerated pointer coordinates][libinput-raw] and
  [custom acceleration profiles][libinput-custom].
- Hyprland documents its [input variables][hypr-variables] and
  [plugin installation model][hypr-plugins].
- [libinput-epp] records the earlier reverse engineering that motivated the
  clean implementation.
- [Mark Cranness' analysis][markc] records the non-EPP sensitivity mapping.
- [Winbindex] locates binaries by Microsoft update metadata; PDBs came from
  Microsoft's public symbol server.

[spi]: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-systemparametersinfow
[libinput-raw]: https://wayland.freedesktop.org/libinput/doc/latest/api/group__event__pointer.html
[libinput-custom]: https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html
[hypr-variables]: https://wiki.hypr.land/Configuring/Variables/
[hypr-plugins]: https://wiki.hypr.land/Plugins/Using-Plugins/
[libinput-epp]: https://gitlab.freedesktop.org/tehabstract/libinput-epp
[markc]: https://www.esreality.com/post/1846538/markc-windows-7-mouse-acceleration-fix/
[Winbindex]: https://github.com/m417z/winbindex
