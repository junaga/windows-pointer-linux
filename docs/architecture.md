# where the numbers go

```text
mouse
  └─ evdev report: integer REL_X / REL_Y counts
      └─ libinput
          ├─ delta: libinput's accelerated movement
          └─ unaccel: raw device-coordinate movement
              └─ windows-pointer-linux
                  └─ replaces delta with Windows output
                      └─ Hyprland cursor and normal Wayland pointer events
```

The plugin hooks `CInputManager::onMouseMoved` immediately before Hyprland
consumes a relative pointer event. For physical mice it feeds the event's
unaccelerated device coordinates into the engine and replaces only the
accelerated delta. The original Hyprland function still handles the event.
Touchpads, virtual pointers, absolute devices, and malformed coordinates pass
through unchanged.

The engine is a separate static library with no Hyprland or Linux dependency.
It owns the session-wide Windows state:

- enhanced and linear signed subpixel remainders;
- the previous enhanced-curve segment;
- curve coefficients for the current display DPI;
- the two public Windows settings.

One engine is shared by all physical mice because Windows shares this state
for the desktop cursor. Device DPI and polling rate are not configuration
inputs. They are already present in the counts and report boundaries.

Before each report the adapter asks Hyprland which monitor contains the
cursor. A Hyprland scale of `s` becomes an effective Windows display DPI of
`round(s * 96)`, clamped to Windows' supported 96–480 DPI range.

Arbitrary libinput device rotation can turn integer counts into fractional
coordinates. Windows' input to this algorithm is integer, so pretending those
fractions are exact and truncating them would lose motion. Such reports pass
through and appear in the `rotated` diagnostic counter. Quarter-turn rotations
normally remain integral and continue to work.

## what it intentionally does not touch

Applications can ask Wayland for unaccelerated relative motion. Games often do.
That stream remains raw by design, just as Windows Raw Input is separate from
the desktop cursor. This project changes the compositor cursor path, not every
application's private idea of mouse input.

`input.force_no_accel` tells Hyprland to consume the unaccelerated stream and
therefore bypasses the replacement delta. Leave it disabled. Libinput
`sensitivity` and `accel_profile` no longer shape the normal cursor while the
plugin is active because their accelerated result is replaced.

The default Windows registry curve is implemented. User-edited
`SmoothMouseXCurve` and `SmoothMouseYCurve`, touchpad precision gestures, pen
input, and application raw input are outside this project's promise.

## why a plugin

The arithmetic is around a hundred lines. Getting the correct report before
one acceleration filter and after another is the actual project.

A compositor plugin has one ugly property: Hyprland's C++ ABI is intentionally
unstable, so it must be rebuilt after a Hyprland update. In return it needs no
kernel module, patched system library, privileged daemon, grabbed device, or
fake permanent mouse. For a Hyprland-specific project, that is the smallest
honest blast radius.
