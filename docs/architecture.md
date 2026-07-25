# architecture

`windows-pointer-linux` has two layers with one strict boundary:

- the **engine** implements Windows pointer motion;
- an **adapter** connects that engine to a desktop input path.

The engine has no compositor, Wayland, libinput, operating-system, device, or
event-loop dependency. Adapters have no pointer arithmetic. Keeping those two
facts true is the architecture.

## data flow

```text
physical relative mouse
  └─ raw report: integer X/Y counts
      └─ desktop adapter
          ├─ identifies the display under the cursor
          ├─ passes raw counts and effective DPI to Engine::apply
          └─ replaces the normal accelerated desktop delta
              └─ compositor cursor and normal client pointer events

application-requested raw motion ──────────────────────── remains raw
unsupported device classes ────────────────────────────── pass through
```

The adapter must see one raw two-axis report before its desktop acceleration
result is consumed. Splitting X and Y into separate calls is incorrect:
Windows selects gain from an approximation of the report's two-dimensional
length.

## engine contract

The public API is in `include/windows_pointer/engine.hpp`.

`Engine::apply(Motion raw, uint16_t displayDpi)` accepts one signed integer
report and returns one signed integer desktop delta. `Engine::configure`
changes the two public Windows settings without discarding accumulated motion
state. `Engine::reset` deliberately clears that state.

The engine owns:

- the default Windows curve constants and fixed-point arithmetic;
- the complete 1–20 pointer-speed mapping;
- enhanced and linear signed subpixel remainders;
- the previous enhanced-curve segment;
- cached curve coefficients for the active display DPI;
- the current pointer-speed and EPP settings.

It does not own:

- device discovery, permissions, hotplug, or event delivery;
- monitor geometry or display-scale policy;
- cursor position;
- device classification;
- application raw-input routing;
- diagnostics or user-facing configuration syntax.

Those are adapter responsibilities because every input stack exposes them
differently.

## state ownership

Windows keeps segment history and subpixel remainders in session input-manager
state, not inside an individual mouse. Every physical mouse driving one
desktop pointer therefore shares one engine.

An adapter should create one engine per independently moving desktop pointer,
normally one per compositor seat. Creating one engine per device changes the
result when a user switches mice. Sharing one engine across unrelated seats
also changes the result. The unit is the cursor, not the hardware.

Linear and enhanced motion use separate remainder state. Switching EPP does
not erase either accumulator. Reconfiguring pointer speed rebuilds the curve
when needed but preserves report history. A zero report returns zero and does
not modify segment or remainder state.

## display DPI

Windows builds acceleration curves for input-space regions and selects the
region containing the cursor for each report. The adapter must therefore pass
the effective DPI of the cursor's current display, not the physical DPI of the
mouse.

The helper `displayDpiFromScale(scale)` converts a logical display scale to
`round(scale * 96)` and clamps it to the audited Windows range of 96–480 DPI.
Adapters with a more direct effective-DPI source may pass that value instead.

Mouse CPI/DPI and polling rate never enter this interface. Windows' audited
routine does not consume them. Mouse resolution already affects reported
counts; polling behavior already affects report boundaries.

## adapter contract

A correct desktop adapter performs these steps for every relative event:

1. reject or pass through non-physical, absolute, malformed, or unsupported
   input;
2. preserve the original X/Y report as signed integers;
3. select the display containing the cursor and derive its effective DPI;
4. call the engine associated with that logical desktop pointer;
5. replace only the normal accelerated desktop delta;
6. continue through the platform's ordinary event path.

It must not:

- feed already accelerated motion into the engine;
- coalesce or split reports before calling the engine;
- create state per physical mouse;
- use physical mouse DPI as display DPI;
- apply the result to application-requested raw input;
- truncate fractional coordinates introduced by an unsupported transform.

Fractional transformed reports cannot be represented faithfully by the
integer Windows routine. Passing them through is safer than quietly deleting
motion. Quarter-turn rotations normally remain integral.

## the shipped adapter

The first adapter hooks Hyprland's `CInputManager::onMouseMoved` immediately
before a relative pointer event is consumed. For physical mice it reads
libinput's unaccelerated device coordinates, asks the compositor which monitor
contains the cursor, runs the shared engine, and replaces `event.delta`. The
original compositor function then handles the event normally.

Touchpads, virtual pointers, missing devices, malformed coordinates, and
non-integral transformed coordinates pass through. Status counters make every
decision visible through `hyprctl windows-pointer-linux`.

Hyprland's plugin ABI is intentionally unstable. The adapter verifies the
complete compositor ABI hash at load time and refuses a mismatched build.
That rebuild cost is confined to `src/plugin.cpp`; the engine and its tests do
not depend on the compositor ABI.

`input.force_no_accel` asks Hyprland to consume the unaccelerated stream and
therefore bypasses the replaced desktop delta. It must remain disabled.
Libinput `sensitivity` and `accel_profile` do not shape the normal cursor while
the adapter is active because their accelerated result is replaced.

## porting to another compositor

A port needs a small adapter, not another mouse-acceleration project:

1. locate the last input hook that still exposes both the raw integer report
   and the desktop delta;
2. identify the platform's logical pointer or seat lifetime;
3. map the cursor's display region to effective DPI;
4. classify physical relative mice and explicit raw-input paths;
5. translate two settings into `windows_pointer::Settings`;
6. add integration diagnostics and one synthetic end-to-end test.

If the compositor does not expose such a hook, viable alternatives are an
upstream compositor patch, an enabled libinput Lua context, or a userspace
evdev/uinput adapter. Those choices change deployment and failure recovery,
not the engine.

The hard part—the Windows state machine—is already isolated in
`windows_pointer::Engine`. Please do not lovingly reimplement it inside a
second event callback.

## scope boundary

The engine implements the default Windows 11 registry curve. Custom
`SmoothMouseXCurve` and `SmoothMouseYCurve` data, touchpad precision gestures,
pen acceleration, absolute devices, and application-defined acceleration are
outside version 1.0.

Applications may request unaccelerated relative motion. That stream remains
raw by design, matching the separation between the Windows desktop cursor and
Windows Raw Input.
