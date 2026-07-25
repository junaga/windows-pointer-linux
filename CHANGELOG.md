# changelog

## 1.0.0

- implements the default Windows 11 EPP fixed-point curve and every 1–20
  pointer-speed position;
- follows the cursor across per-monitor display scaling;
- preserves Windows' session-wide state across physical mice and setting
  changes;
- passes touchpads, virtual pointers, raw application input, and unsupported
  fractional device rotation through unchanged;
- exposes Lua configuration plus `hyprctl` status and reset diagnostics;
- includes an independent reference model, exhaustive settings tests, human
  motion scenarios, sanitizers, a fuzzer, replay tools, a native benchmark,
  and a live uinput/Hyprland test;
- ships CMake presets, install rules, hyprpm metadata, and Linux/Windows CI.

yes, version zero took the scenic route.
