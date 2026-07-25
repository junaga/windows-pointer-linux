# windows-pointer-linux

Windows 11 desktop pointer motion for Hyprland. finally fixed, because
apparently even moving a cursor had to remain a platform exclusive.

This plugin takes the raw integer reports Hyprland already receives from a
physical mouse and replaces libinput's accelerated cursor delta with the
current Windows fixed-point algorithm. It implements both public Windows 11
controls:

- pointer speed from `"1/20"` through `"20/20"`;
- Enhance pointer precision on or off.

No device DPI. No polling-rate questionnaire. Both are already encoded in the
counts and report boundaries produced by the mouse. Asking for them again
would be extremely configurable double counting.

To the best of the public source search documented below, this is the first
exact Windows 11 pointer implementation for Hyprland. It is not the first
project to make a Linux cursor vaguely Windows-flavoured. That achievement has
been independently rediscovered through lookup tables, curve generators,
patched system libraries, kernel modules, and at least one Raspberry Pi. We
finished the desktop behaviour instead.

## what is actually implemented

Version 1.0 implements:

- the default EPP curves audited in two shipping `win32kbase.sys` builds;
- the complete 1–20 pointer-speed mapping with EPP enabled or disabled;
- Windows' fixed-point arithmetic and signed subpixel remainders;
- previous-segment state and one-report upward-transition smoothing;
- zero-report behaviour;
- session-wide state shared by all physical mice;
- effective DPI selected from the monitor currently under the cursor;
- direct replacement of Hyprland's normal desktop cursor delta;
- unchanged raw relative motion for applications and games.

The implementation target and binary hashes are recorded in
[the Windows audit](docs/research/windows-11.md). This is private Windows
behaviour, not a Microsoft API contract. If Microsoft changes it later, we get
to reverse-engineer a mouse again. the future remains bright.

## why a custom curve is not enough

Windows does not evaluate a static function of velocity. For every integer
X/Y report it:

1. computes an approximate two-dimensional distance;
2. selects a piecewise curve segment;
3. smooths the first transition into a faster segment;
4. applies fixed-point gain independently to both axes;
5. carries signed fractional output into later reports;
6. remembers the segment for the next report.

Libinput custom profiles receive libinput's calculated velocity. They cannot
represent the previous segment, the remainders, the zero-report rule, or the
original report boundaries. A curve can resemble Windows. It cannot become
Windows by adding more decimal places and confidence.

## what came before

There is real prior art here, and it deserves links instead of a victory lap
over imaginary opponents. None of the following projects is useless. None
implements the same complete current Windows 11 desktop behaviour either.

- [`libinput-epp`](https://gitlab.freedesktop.org/tehabstract/libinput-epp)
  is the closest predecessor. Its EPP core should match under narrow
  conditions: one mouse and 96 DPI display scaling. But it replaces system
  libinput, requires privileged package/library installation, implements only
  EPP, keeps state per device, changes previous-segment state on zero reports,
  and cannot follow per-monitor DPI. The patch also replaces libinput's normal
  adaptive fallback rather than exposing a complete independently selectable
  Windows profile, and it has no regression, scaling, multi-device, sanitizer,
  or state-transition test suite.

- [`libpointing`](https://github.com/INRIA/libpointing) is the oldest serious
  Linux implementation found, originating in the 2011 UIST paper
  [*No More Bricolage!*](https://doi.org/10.1145/2047196.2047276). It provides
  empirically measured Windows EPP and non-EPP transfer functions to
  applications reading raw HID input. It does not replace the Wayland desktop
  cursor. Its Windows backend interpolates measured tables using Euclidean
  magnitude, floating-point remainders, and direction-change resets rather
  than the current fixed-point Windows state machine.

- [`SmoothMouse`](https://github.com/spamwax/SmoothMouse) brought
  libpointing-derived Windows acceleration to macOS system-wide. It is macOS
  only, was retired in 2013, and implements the older Windows 7-era
  floating-point model with fixed 96 DPI and 60 Hz assumptions.

- [`hidlikewindows`](https://github.com/temuera/hidlikewindows) runs an older
  Windows-like floating-point formula through one or two Raspberry Pis acting
  as a USB proxy. It has fixed display assumptions and processes `REL_X` and
  `REL_Y` evdev events as separate acceleration calls, losing the
  two-dimensional report semantics Windows uses. The hardware requirement is
  admirably committed to avoiding a compositor plugin.

- [Hyprland's Windows curve generator][hypr-generator] is a convenience fork
  of [the original libinput generator][original-generator]. It samples the
  registry curve into a libinput custom profile. Both sources explicitly say
  the scaling calculation is guessed; the original author also says the exact
  Windows formula is unavailable. Sampling a curve cannot preserve report
  state or signed remainders, regardless of sample count.

- [Libinput custom profiles][libinput-custom] are the cleanest configuration
  mechanism when “roughly this shape” is enough. They evaluate a sampled gain
  function over libinput velocity, not the Windows integer-report state
  machine.

- [Libinput Lua plugins][libinput-lua] can alter evdev frames without carrying
  a permanent libinput fork. This is a promising cross-compositor layer when
  the consumer explicitly enables the plugin context. Hyprland currently does
  not.

- [`mouse-sync`](https://github.com/ShouldBeLightWay/mouse-sync) transports
  Windows mouse settings into KDE. Its Linux backend writes KWin's native
  pointer speed and acceleration profile; KWin still executes libinput's
  algorithm. It synchronizes settings data, not the behaviour behind the
  settings.

- [`YeetMouse`](https://github.com/AndyFilter/YeetMouse) is a configurable
  Linux kernel acceleration module. Its EPP guide converts a velocity curve
  into a sensitivity lookup table because velocity LUTs are unsupported. A LUT
  still cannot reproduce report-to-report Windows state. Installation requires
  root, kernel headers, DKMS/module management, and disabling desktop
  acceleration.

- [`LeetMouse`](https://github.com/systemofapwne/leetmouse), YeetMouse's
  deprecated predecessor, provides Quake-style kernel acceleration rather than
  the Windows desktop algorithm. It carries the same privileged DKMS and
  kernel-compatibility cost.

- [`maccel`](https://github.com/Gnarus-G/maccel) is a maintained kernel module
  for designing linear, natural, and synchronous acceleration curves. It is
  intentionally a better custom-acceleration laboratory, not a Windows
  compatibility implementation. It also requires privileged DKMS
  installation.

- [Xorg pointer acceleration][xorg-accel] provides server-side threshold,
  polynomial, simple, smooth-linear, and device profiles. These are Xorg's
  algorithms. A native Wayland cursor does not pass through them, and an
  XWayland client cannot configure the compositor's pointer.

- [Raw Accel](https://github.com/RawAccelOfficial/rawaccel) is a mature Windows
  driver for programmable raw-input acceleration. It runs on Windows, changes
  application raw input rather than reproducing the default desktop cursor on
  Linux, and describes its own EPP LUT matching as very close rather than
  exact.

- [MarkC's Windows analysis][markc] documents the Windows slider mappings and
  registry curves and provides registry fixes for Windows itself. It is
  invaluable research, not a Linux input implementation.

- A userspace [uinput][uinput] daemon could be compositor-independent and
  exact. It would also need exclusive evdev grabs, permissions, hotplug
  tracking, loop prevention, replacement devices, a background service, and
  failure recovery that returns the real mouse when the daemon dies. No public
  daemon implementing the complete current Windows state machine was found.

This plugin chooses the smallest layer with both facts the algorithm needs:
the original report and the final desktop cursor destination. The result needs
no root access, kernel module, system-library replacement, permanent fake
mouse, device-specific DPI value, or polling-rate estimate. The price is
Hyprland-only support and a rebuild when Hyprland's C++ ABI changes.

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

Load the absolute path:

```lua
local plugin = os.getenv("HOME")
    .. "/.local/lib/hyprland/plugins/windows-pointer-linux.so"

hl.plugin.load(plugin)
```

Hyprland plugins use an unstable C++ ABI. The plugin verifies the complete ABI
hash and refuses to load against another compositor build. Rebuild it after a
Hyprland update. annoying, but significantly nicer than debugging a stale
vtable with your cursor.

### hyprpm

```sh
hyprpm add https://github.com/junaga/windows-pointer-linux
hyprpm enable windows-pointer-linux
hyprpm reload
```

### rebuilding a loaded plugin

Unload before overwriting the shared object:

```sh
hyprctl plugin unload \
  "$HOME/.local/lib/hyprland/plugins/windows-pointer-linux.so"
cmake --build --preset release
cmake --install build/release --prefix "$HOME/.local"
hyprctl plugin load \
  "$HOME/.local/lib/hyprland/plugins/windows-pointer-linux.so"
```

Hyprland queues a configuration reload after plugin load and unload, so the
Lua settings are reapplied automatically.

Use the exact pathname string originally passed to `hl.plugin.load`.
Hyprland's unload lookup compares paths, not files: a symlink, bind-mounted
alias, or other path to the same inode still receives the wonderfully precise
answer `plugin not loaded`.

Do not overwrite a loaded `.so`. Hyprland is executing it inside the
compositor, and lazy page faults are an exciting place to discover half of a
new binary.

## configure

The guard handles Hyprland's two-phase Lua plugin load during a fresh session:

```lua
if hl.plugin.windows_pointer_linux then
    hl.config({
        plugin = {
            windows_pointer_linux = {
                pointer_speed = "10/20",
                enhance_pointer_precision = true,
            },
        },
    })
end
```

`"10/20"` with EPP enabled is the Windows 11 default. Configuration reloads
preserve the motion accumulators, matching Windows. Invalid speed strings are
rejected and reported as a Hyprland notification instead of becoming a
creative new input mode.

Leave `input.force_no_accel` disabled. That option explicitly makes Hyprland
consume raw motion instead of the accelerated delta, including ours. Libinput
`sensitivity` and `accel_profile` no longer shape the normal cursor while the
plugin is active because their output is replaced.

## inspect it

```sh
hyprctl windows-pointer-linux
hyprctl -j windows-pointer-linux
hyprctl windows-pointer-linux reset
```

Status includes the active settings, pass-through reasons, per-device report
counts, current display DPI, and the last raw/output motion. `reset` clears
diagnostics and the session pointer state, preserving the ancient debugging
ritual without unloading actual machine code.

## scope

The plugin changes normal cursor motion from physical mice.

Touchpads, Wayland virtual pointers, absolute devices, malformed coordinates,
and fractional raw coordinates produced by arbitrary device rotation pass
through unchanged. Quarter-turn rotations normally remain integral.

Applications can explicitly request unaccelerated relative motion. Games often
do. That path remains raw by design, just as Windows Raw Input is separate from
the desktop cursor. This project fixes the cursor; it does not sneak
acceleration into a game's private camera input.

The default Windows registry curve is implemented. User-edited
`SmoothMouseXCurve` and `SmoothMouseYCurve`, touchpad precision gestures, pen
input, and application-defined acceleration are outside the promise.

See [architecture](docs/architecture.md) for the exact hook and state
ownership.

## testing

The engine is independent from Hyprland and is checked against a separate
straight-line model across:

- all 20 pointer speeds, with EPP enabled and disabled;
- 96, 120, 144, 192, and 480 DPI display regions;
- thousands of reports per configuration;
- zero reports, segment transitions, signed remainders, and setting changes;
- the complete signed evdev coordinate range;
- three extremely formal scenarios involving Discord, Hypixel, and dragging a
  file into a website.

The project also runs address/undefined-behaviour sanitizers, libFuzzer, native
trace replay, a microbenchmark, Clang static analysis, and a live synthetic
mouse through uinput, libinput, and a running Hyprland session.

The commands and expected boundaries are in [testing](docs/testing.md).

## develop

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

See [contributing](CONTRIBUTING.md) before changing fixed-point arithmetic.
Small commits are preferred. Lowercase sarcasm is welcome. Unexplained magic
numbers remain illegal even when they feel correct.

The project is BSD-3-Clause licensed. No Microsoft binary, PDB, registry
export, or source code is distributed here.

## primary references

- Microsoft documents the public
  [`SystemParametersInfo` mouse settings][spi].
- Libinput documents [raw unaccelerated pointer coordinates][libinput-raw],
  [custom acceleration profiles][libinput-custom], and
  [Lua plugins][libinput-lua].
- Hyprland documents its [input variables][hypr-variables] and
  [plugin installation model][hypr-plugins].
- [`libinput-epp`](https://gitlab.freedesktop.org/tehabstract/libinput-epp)
  records the earlier fixed-point EPP work.
- [Mark Cranness' analysis][markc] records the non-EPP sensitivity mapping.
- [Winbindex] locates binaries by Microsoft update metadata; PDBs came from
  Microsoft's public symbol server.

[spi]: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-systemparametersinfow
[libinput-raw]: https://wayland.freedesktop.org/libinput/doc/latest/api/group__event__pointer.html
[libinput-custom]: https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html#the-custom-profile
[libinput-lua]: https://wayland.freedesktop.org/libinput/doc/latest/lua-plugins.html
[hypr-variables]: https://wiki.hypr.land/Configuring/Variables/
[hypr-plugins]: https://wiki.hypr.land/Plugins/Using-Plugins/
[hypr-generator]: https://gist.github.com/Bugg4/9c9f43c9d06ee678c716986efaf6f170
[original-generator]: https://gist.github.com/yinonburgansky/7be4d0489a0df8c06a923240b8eb0191
[xorg-accel]: https://xorg.freedesktop.org/wiki/Development/Documentation/PointerAcceleration/
[markc]: https://www.esreality.com/post/1846538/markc-windows-7-mouse-acceleration-fix/
[uinput]: https://www.kernel.org/doc/html/latest/input/uinput.html
[Winbindex]: https://github.com/m417z/winbindex
