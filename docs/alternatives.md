# other ways people have attacked the mouse

This is the map of serious approaches found during the project. “not exact
Windows EPP” does not mean “bad.” It means the thing solves a different problem
and should not be sold as this one.

| approach | installation | scope | why it is not this implementation |
| --- | --- | --- | --- |
| Hyprland/libinput custom profile | one config string | one compositor/device | samples a gain function over libinput velocity; Windows consumes integer reports with segment and remainder state |
| Hyprland-linked Python curve generator | run a script, paste points | Hyprland | approximates the registry curve as custom samples, asks for device DPI, and its own source says the scaling math is guessed |
| `libinput-epp` | rebuild/replace libinput | every compositor using that library | closest prior implementation; system-wide library fork and its patch uses a fixed display reference |
| libinput Lua plugin | install Lua under `/etc`, compositor must enable plugins | every opted-in libinput consumer | excellent layer and no fork, but Hyprland does not currently enable the plugin context |
| this Hyprland plugin | rebuild one `.so` with Hyprland | Hyprland cursor | exact audited state machine, small scope, pays the unstable compositor ABI tax |
| uinput interception daemon | permissions, service, exclusive device grab | compositor-independent | can be exact, but must suppress real devices and create replacements; hotplug and failure recovery become a second input stack |
| YeetMouse / LeetMouse / maccel | DKMS/kernel module, service or UI | below every desktop | powerful Raw Accel/Quake-style configurable curves; not the Windows desktop EPP state machine and intentionally much larger scope |
| Xorg `xinput` / `xset` | session commands or Xorg config | X11 only | configures X/libinput algorithms; native Wayland cursor motion never goes through it |
| application or game acceleration | game setting/config | that application | useful for camera motion, invisible to the desktop cursor |
| Raw Accel | Windows driver and reboot | Windows raw-input path | a Windows custom-acceleration product, not a Linux implementation of Windows' default desktop pointer |

## the custom-profile family

Libinput's custom profile is a sampled function of the velocity libinput
calculates. That is a good public API and the simplest way to get a
Windows-*shaped* curve. Hyprland's variables documentation links a
[Python generator] that emits such a profile.

The generator is not an exact port. It converts the registry points to sampled
floating-point gains, depends on the device DPI and libinput time units, and
contains explicit comments that guesses were made and the calculation is not
accurate. Windows' audited `Accelerate` routine receives no DPI, report rate,
or timestamp. It sees integer X/Y reports, carries fixed-point remainders, and
smooths upward segment changes for one report. A static velocity curve cannot
represent all of that state.

## the libinput family

[libinput-epp] put the reverse-engineered state machine directly into a
libinput fork. Architecturally this is broad and sensible: every compositor
gets it. Operationally it means replacing a foundational system input library
and keeping the fork synchronized. It was the most valuable reference for this
project, but a dotfile should not quietly become a distro.

Modern [libinput Lua plugins] can alter evdev frames before libinput processes
them. This would be the nicest cross-compositor local extension point if the
consumer enabled it. Plugins are opt-in because they execute code inside
libinput; Hyprland's input backend currently creates libinput without enabling
that context.

## fake and kernel mice

A userspace daemon can grab each evdev mouse, apply the algorithm, and emit a
uinput device. It works under Wayland because the compositor sees an ordinary
kernel pointer. It also needs device permissions, hotplug tracking, exclusive
grabs, loop prevention, a service, and careful recovery if the daemon dies.
That is a reasonable project when compositor independence matters. It is a
rather elaborate fake mouse when it does not.

[YeetMouse], deprecated [LeetMouse], and [maccel] live even lower as kernel
modules. They are aimed at configurable Raw Accel or Quake-style motion and can
affect applications beyond one compositor. They require matching kernel
headers/DKMS and deserve exactly the scrutiny any input kernel module deserves.
YeetMouse documents an EPP-like LUT, but also documents that velocity LUTs are
not directly supported; converting a curve does not add Windows' report state.

## things that are simply elsewhere

`xinput`, `xset`, and Xorg driver properties still matter in an X11 session.
XWayland applications do not get to configure the native Wayland compositor's
cursor with them.

[Raw Accel] is frequently suggested because it is excellent at customizable
Windows raw-input acceleration. It runs on Windows and intentionally offers
different algorithms. Matching one of its LUT graphs closely is not the same
as implementing the private Windows desktop cursor routine.

[Python generator]: https://gist.github.com/Bugg4/9c9f43c9d06ee678c716986efaf6f170
[libinput-epp]: https://gitlab.freedesktop.org/tehabstract/libinput-epp
[libinput Lua plugins]: https://wayland.freedesktop.org/libinput/doc/latest/lua-plugins.html
[YeetMouse]: https://github.com/AndyFilter/YeetMouse
[LeetMouse]: https://github.com/systemofapwne/leetmouse
[maccel]: https://github.com/Gnarus-G/maccel
[Raw Accel]: https://github.com/RawAccelOfficial/rawaccel
