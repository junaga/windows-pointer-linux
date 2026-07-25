# what Windows actually does

There are many descriptions of Enhance pointer precision online. Most of them
eventually become folklore about polling rates. This document records what the
shipping Windows 11 code does instead.

No Microsoft binary or PDB is distributed with this project. The notes below
are a clean implementation record: symbol names, constants, arithmetic, and
observable state. Please bring your own copy of Windows if staring at kernel
assembly is how you relax.

## samples audited

Two Microsoft symbol-server copies of `win32kbase.sys` were checked so this
would not quietly depend on one old build:

<!-- markdownlint-disable MD013 -->

| Windows release | File version | `win32kbase.sys` SHA-256 | PDB SHA-256 |
| --- | --- | --- | --- |
| Windows 11 24H2 | 10.0.26100.3194 | `3797988de774548b62d330b0d83650157ede04ad561c358287c6e2efdf33d571` | `99b74efaf282361daec28f18366321164cf6e31887fbc8d4e88a25ea8fe1ea9e` |
| Windows 11 26H1, KB5101649 | 10.0.28000.2525 | `2ffeb3fd6aa653a3254c18dc0208e15d518ad348b1fe4477fb30f326de31419b` | `2796cd48c57b8dc9c3c49464539859f883feaec82a5c33193499ab4b7a797c4b` |

<!-- markdownlint-enable MD013 -->

The files were located through [Winbindex], downloaded from Microsoft's public
symbol server, and matched against the hashes in the corresponding Windows
update manifests. LLVM's PDB and COFF tools were used for inspection.

The relevant public symbols exist in both builds:

- `CDeviceAcceleration::Accelerate`
- `CDeviceAcceleration::_BuildAccelerationCurve`
- `CDeviceAcceleration::CreateDefaultAcceleratorCurve`
- `CMouseAcceleration::BuildAccelerationCurve`
- `CMouseAcceleration::GetDeviceSpecificAccelerationData`
- `CMouseAcceleration::MOUSE_SENSITIVITY_INFO::UpdateMouseSensitivity`
- `CDeviceAcceleration::ResetAccelerationCurves`

## fixed curve data

Windows reads `SmoothMouseXCurve` and `SmoothMouseYCurve` from the user
profile. If either value is unavailable, both audited builds install these
five-point defaults:

```text
x = 0, 0x6e15, 0x14000, 0x3dc29, 0x280000
y = 0, 0x111fd, 0x42400, 0x12fc00, 0x1bbc000
```

The default curve is what this project implements. A deliberately customized
Windows registry curve is outside the project's promise.

For a Windows pointer-speed value `s` and display-region DPI `d`, the curve
builder uses Q16 fixed-point arithmetic:

```text
speed factor = (s << 16) / 10
dpi factor   = (max(d, 96) << 16) / 120
scaled x     = (x * 0x38000) >> 16
scaled y     = (((dpi factor * y) >> 16) * speed factor) >> 16
```

It then derives four line slopes and intercepts. These operations and constants
are unchanged between the two audited releases.

## each input report

`Accelerate` receives integer X and Y counts plus subpixel output storage. It
does not receive a timestamp, elapsed duration, report rate, or device DPI.
That rules out the popular explanation in which Windows estimates physical
mouse speed from Hz.

For each non-zero report Windows:

1. converts both axes to Q16;
2. approximates vector length as `max(abs(x), abs(y)) + min(abs(x), abs(y)) / 2`;
3. selects one of the four curve segments;
4. evaluates that segment as `slope + intercept / distance`;
5. when moving to a higher segment, averages that gain with the gain from the
   previous segment for one report;
6. applies the gain and carries signed subpixel remainders into the next
   report.

A zero report leaves segment history and remainders alone.

Raw mouse counts already contain the mouse's hardware CPI/DPI. The boundaries
between reports already contain its reporting behavior. Asking the user for
either number and applying it again would distort the Windows algorithm.

## display regions and shared state

`ResetAccelerationCurves` builds ballistics for every configured input-space
region using that region's effective display DPI. `Accelerate` selects the
region containing the cursor before processing the report.

The current segment and enhanced-motion subpixel remainders live in the
session input-manager state, not in the individual mouse object. In other
words, two physical mice moving the same Windows cursor share motion history.
Linear and enhanced motion keep separate remainder state, which survives
switching the Enhance pointer precision setting.

A desktop adapter must supply the same ingredients: raw device coordinates and
the effective DPI of the display currently containing the cursor. The shipped
adapter derives effective DPI as `round(display scale * 96)`. Other
compositors may expose the same value differently. The engine preserves the
session-wide state independently of that integration.

## Enhance pointer precision off

Without EPP, Windows stores the speed factor in Q8. The exact 1–20 mapping is:

```text
1..2   => speed / 32
3..10  => (speed - 2) / 8
11..20 => (speed - 6) / 4
```

This gives `1.0` at the Windows 11 default of `10/20`. Fractional results are
carried between reports.

## audited version boundary

The engine is exact to the default arithmetic and state machine in both
shipping Windows 11 releases listed above. The relevant constants and
operations are identical between them.

This routine is private kernel behavior, not a Microsoft API guarantee. A
future Windows build can change it. The hashes define the implementation target
precisely, and the independent test model makes any future update explicit
instead of letting the result drift quietly.

[Winbindex]: https://github.com/m417z/winbindex
