# windows-pointer-linux

**The Windows 11 desktop pointer algorithm, rebuilt for Linux.**

Windows pointer motion is not an acceleration curve. It is a fixed-point,
report-to-report state machine. `windows-pointer-linux` implements that state
machine: all 20 pointer speeds, Enhance pointer precision on and off, signed
subpixel remainders, segment transitions, shared mouse state, and per-display
scaling.

The engine is a small, platform-independent C++ library. Version 1.0 ships its
first desktop adapter for Hyprland. The engine itself does not know what
Hyprland, Wayland, libinput, or even Linux is. A cursor was apparently still
platform-exclusive software. it isn't now.

## use it today: Hyprland

Install and enable the adapter with `hyprpm`:

```sh
hyprpm add https://github.com/junaga/windows-pointer-linux
hyprpm enable windows-pointer-linux
hyprpm reload
```

Configure the same two controls exposed by Windows 11:

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

`"10/20"` with EPP enabled is the Windows default. Leave
`input.force_no_accel` disabled; it explicitly asks the compositor to bypass
the accelerated desktop delta, including this one.

Inspect the running adapter with `hyprctl windows-pointer-linux` or its JSON
form, `hyprctl -j windows-pointer-linux`. The `reset` argument clears its
diagnostics and session motion state. Hyprland plugins follow Hyprland's
unstable C++ ABI, so rebuild the adapter after a compositor update. No root
access, kernel module, system-library replacement, background daemon, or fake
mouse is involved.

## what exact means

The implementation follows the private Windows 11 desktop routine audited in
two shipping builds:

- Windows 11 24H2, `win32kbase.sys` 10.0.26100.3194;
- Windows 11 26H1, `win32kbase.sys` 10.0.28000.2525.

Exact here means the same default curve constants, integer operations,
slider mappings, state transitions, and state ownership observed in those
builds. It does not mean “a curve that looked about right after moving the
mouse in circles.”

Version 1.0 implements:

- every pointer-speed position from `1/20` through `20/20`;
- Enhance pointer precision enabled and disabled;
- the default Windows fixed-point acceleration curve;
- signed subpixel accumulation on both axes;
- previous-segment state and one-report upward-transition smoothing;
- the Windows zero-report rule;
- state shared by every physical mouse controlling one desktop pointer;
- effective DPI selected from the display containing the cursor;
- replacement of the accelerated desktop delta without changing application
  raw input.

The complete implementation record, including binary and PDB hashes, is in
[the Windows audit](docs/research/windows-11.md). No Microsoft binary, PDB,
registry export, or source code is distributed here.

Mouse DPI and polling rate are deliberately not settings. Windows' routine
does not receive either value. Hardware resolution is already encoded in the
counts a mouse reports, and reporting behavior is already encoded in the
report boundaries. Asking for those numbers again would double-count them
with a very professional-looking settings dialog.

## why a custom curve cannot do this

For each integer X/Y report, Windows:

1. approximates the two-dimensional report distance;
2. selects a piecewise curve segment;
3. smooths the first transition into a faster segment;
4. applies fixed-point gain independently to both axes;
5. carries signed fractional output into later reports;
6. remembers the segment for the next report.

A libinput custom profile receives libinput's calculated velocity and returns
a gain. It cannot recover the original report state, previous segment, signed
remainders, or zero-report behavior. Sampling the curve more densely only
produces a more densely sampled approximation.

This distinction also explains why a static `accel_profile` string was never
going to finish the job. The missing part was not another decimal place. It
was memory.

## one engine, small adapters

The portable engine accepts one raw integer motion report and the effective
DPI of the display under the cursor, then returns one accelerated integer
desktop delta. It owns the Windows arithmetic and all report-to-report state.

The platform adapter owns only the surrounding input facts:

| Responsibility | Engine | Adapter |
| --- | :---: | :---: |
| Windows curve and pointer-speed math | yes | |
| Segment history and signed remainders | yes | |
| EPP and non-EPP behavior | yes | |
| Receive raw relative mouse reports | | yes |
| Identify the cursor's display DPI | | yes |
| Replace the normal desktop delta | | yes |
| Preserve raw application input | | yes |
| Pass through touchpads, pens, and virtual pointers | | yes |

To support another compositor, an adapter must:

1. intercept the raw integer X/Y report for a physical relative mouse;
2. determine the effective DPI of the display containing the cursor;
3. keep one `windows_pointer::Engine` per logical desktop pointer or seat;
4. replace only the compositor's accelerated desktop delta with
   `Engine::apply`;
5. leave application-requested raw motion and unsupported device classes
   untouched.

That is the entire integration contract. Porting does not require
reverse-engineering Windows again, modifying the engine, replacing libinput,
or teaching users the polling rate of a mouse they bought six years ago. See
[architecture](docs/architecture.md) for the data path, invariants, and
adapter checklist.

## why this project had to exist

Linux already had Windows-like curves, settings synchronizers, application
libraries, patched input stacks, kernel modules, and hardware proxies. It did
not have one complete, practical implementation of the current Windows 11
desktop state machine.

The existing work falls across four boundaries:

| Approach | Reproduces | Missing |
| --- | --- | --- |
| sampled curves | gain shape | report state and exact output |
| application libraries | one client's motion | the desktop cursor |
| input-stack replacements | system-wide control | small, unprivileged setup |
| settings synchronizers | slider values | Windows behavior |

The detailed comparison matters because “Windows acceleration for Linux” has
meant several incompatible things:

- [`libinput-epp`](https://gitlab.freedesktop.org/tehabstract/libinput-epp) is
  the closest predecessor. Its EPP core should match under narrow conditions:
  one mouse and 96 DPI display scaling. It replaces system libinput, requires
  privileged package or library installation, implements only EPP, keeps
  state per device, changes previous-segment state on zero reports, and cannot
  follow per-monitor DPI. The patch also replaces libinput's normal adaptive
  fallback instead of exposing a complete independently selectable Windows
  profile, and it has no regression, scaling, multi-device, sanitizer, or
  state-transition test suite.

- [`libpointing`](https://github.com/INRIA/libpointing), originating in the
  2011 UIST paper [*No More Bricolage!*][no-more-bricolage],
  provides empirically measured Windows EPP and non-EPP transfer functions to
  applications reading raw HID input. It does not replace the Wayland desktop
  cursor. Its Windows backend interpolates measured tables using Euclidean
  magnitude, floating-point remainders, and direction-change resets instead
  of the current fixed-point Windows state machine.

- [`SmoothMouse`](https://github.com/spamwax/SmoothMouse) brought
  libpointing-derived Windows acceleration to macOS system-wide. It is macOS
  only, was retired in 2013, and implements the older Windows 7-era
  floating-point model with fixed 96 DPI and 60 Hz assumptions.

- [`hidlikewindows`](https://github.com/temuera/hidlikewindows) sends an older
  Windows-like floating-point formula through one or two Raspberry Pis acting
  as a USB proxy. It has fixed display assumptions and processes `REL_X` and
  `REL_Y` evdev events as separate acceleration calls, losing the
  two-dimensional report semantics Windows uses. The hardware requirement is
  an impressively literal solution to a software problem.

- [Hyprland's Windows curve generator][hypr-generator] is a convenience fork
  of [the original libinput generator][original-generator]. It samples the
  registry curve into a libinput custom profile. Both sources say the scaling
  calculation is guessed; the original author also states that the exact
  Windows formula is unavailable. A sampled curve has no report state or
  signed remainders.

- [Libinput custom profiles][libinput-custom] are the cleanest configuration
  mechanism when approximately the same gain shape is enough. They evaluate a
  sampled gain function over libinput velocity, not the Windows
  integer-report state machine.

- [Libinput Lua plugins][libinput-lua] can transform evdev frames without a
  permanent libinput fork. They are a plausible adapter layer where the
  compositor enables the plugin context. They provide the hook, not the
  Windows implementation, and support depends on each libinput consumer.

- [`mouse-sync`](https://github.com/ShouldBeLightWay/mouse-sync) copies Windows
  mouse settings into KDE. Its Linux backend writes KWin's native pointer
  speed and acceleration profile, after which KWin still executes libinput's
  algorithm. It synchronizes settings, not their behavior.

- [`YeetMouse`](https://github.com/AndyFilter/YeetMouse) is a configurable
  Linux kernel acceleration module. Its EPP guide converts a velocity curve
  into a sensitivity lookup table because velocity LUTs are unsupported. A
  LUT cannot reproduce report-to-report Windows state. Installation requires
  root, kernel headers, DKMS or module management, and disabled desktop
  acceleration.

- [`LeetMouse`](https://github.com/systemofapwne/leetmouse), YeetMouse's
  deprecated predecessor, implements Quake-style kernel acceleration rather
  than the Windows desktop algorithm. It carries the same privileged DKMS and
  kernel-compatibility cost.

- [`maccel`](https://github.com/Gnarus-G/maccel) is a maintained kernel module
  for designing linear, natural, and synchronous acceleration curves. It is a
  custom-acceleration laboratory, not a Windows compatibility implementation.
  It also requires privileged DKMS installation.

- [Xorg pointer acceleration][xorg-accel] provides server-side threshold,
  polynomial, simple, smooth-linear, and device profiles. These are Xorg's
  algorithms. Native Wayland cursors do not pass through them, and an XWayland
  client cannot configure its compositor's pointer.

- [Raw Accel](https://github.com/RawAccelOfficial/rawaccel) is a mature Windows
  driver for programmable raw-input acceleration. It runs on Windows, changes
  application raw input rather than reproducing the default desktop cursor on
  Linux, and describes its EPP lookup-table matching as very close rather than
  exact.

- [MarkC's Windows analysis][markc] documents slider mappings and registry
  curves and provides registry fixes for Windows itself. It is indispensable
  research, not a Linux input implementation.

- A userspace [uinput][uinput] daemon could host this engine without compositor
  support. Doing so safely requires exclusive evdev grabs, input permissions,
  hotplug tracking, loop prevention, replacement devices, a background
  service, and failure recovery that returns the real mouse when the daemon
  exits. That is a valid future adapter with a much larger failure surface,
  not a simpler installation method hiding behind the word “portable.”

`windows-pointer-linux` puts the reusable algorithm in a standalone library
and keeps platform interception in an adapter. The current adapter chooses the
smallest layer that has both facts the algorithm needs: the original report
and the final desktop cursor destination.

## scope

The engine implements the default Windows registry curve. Deliberately edited
`SmoothMouseXCurve` and `SmoothMouseYCurve` values are outside the version 1.0
contract.

Desktop adapters should apply it to physical relative mice. Touchpad precision
gestures, pens, absolute devices, virtual pointers, and malformed or
non-integral relative coordinates pass through unchanged in the shipped
adapter.

Applications can request unaccelerated relative motion for games, cameras, or
other private input. That path remains raw by design, just as Windows Raw
Input is distinct from its desktop cursor. This project fixes the cursor. It
does not quietly accelerate somebody's crosshair and call that consistency.

## tested like input infrastructure

The engine is checked against a separate straight-line model across:

- all 20 pointer speeds with EPP enabled and disabled;
- 96, 120, 144, 192, and 480 DPI display regions;
- thousands of reports per configuration;
- zero reports, segment transitions, signed remainders, and setting changes;
- the complete signed evdev coordinate range;
- independent state and explicit session-state reset boundaries;
- scenario traces for answering a Discord call, playing Hypixel PvP, and
  dragging a file into a website, the traditional pillars of computer
  science.

The project also runs GCC, Clang, and MSVC builds, address and undefined
behavior sanitizers, libFuzzer, native trace replay, a microbenchmark, and a
live synthetic mouse through the complete shipped adapter path.

See [testing](docs/testing.md) for the test model, commands, and exact
boundaries.

## project map

- [Windows 11 audit](docs/research/windows-11.md) records the target binaries,
  symbols, constants, arithmetic, and version boundary.
- [Architecture](docs/architecture.md) defines engine ownership and the
  adapter contract.
- [Testing](docs/testing.md) documents conformance, regression, fuzz, replay,
  integration, and performance checks.
- [Contributing](CONTRIBUTING.md) explains how to change the engine or add an
  adapter without turning either into folklore.
- [Changelog](CHANGELOG.md) records released behavior.

The project is [BSD-3-Clause licensed](LICENSE).

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
- [Winbindex] locates binaries through Microsoft update metadata; PDBs came
  from Microsoft's public symbol server.

[spi]: https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-systemparametersinfow
[libinput-raw]: https://wayland.freedesktop.org/libinput/doc/latest/api/group__event__pointer.html
[libinput-custom]: https://wayland.freedesktop.org/libinput/doc/latest/pointer-acceleration.html#the-custom-profile
[libinput-lua]: https://wayland.freedesktop.org/libinput/doc/latest/lua-plugins.html
[hypr-variables]: https://wiki.hypr.land/Configuring/Basics/Variables/
[hypr-plugins]: https://wiki.hypr.land/Plugins/Using-Plugins/
[hypr-generator]: https://gist.github.com/Bugg4/9c9f43c9d06ee678c716986efaf6f170
[original-generator]: https://gist.github.com/yinonburgansky/7be4d0489a0df8c06a923240b8eb0191
[xorg-accel]: https://xorg.freedesktop.org/wiki/Development/Documentation/PointerAcceleration/
[markc]: https://www.esreality.com/post/1846538/markc-windows-7-mouse-acceleration-fix/
[uinput]: https://www.kernel.org/doc/html/latest/input/uinput.html
[Winbindex]: https://github.com/m417z/winbindex
[no-more-bricolage]: https://direction.bordeaux.inria.fr/~roussel/publications/2011-UIST-libpointing.pdf
