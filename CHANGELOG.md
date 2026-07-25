# changelog

## 1.0.0

- ships a platform-independent Windows pointer engine and its first Hyprland
  desktop adapter;
- implements the default Windows 11 EPP fixed-point curve and every 1–20
  pointer-speed position;
- implements non-EPP slider scaling with independent signed remainder state;
- preserves Windows segment history, transition smoothing, zero-report
  behavior, and session-wide state across physical mice and setting changes;
- selects acceleration from the display region currently containing the
  cursor;
- leaves touchpads, virtual pointers, application raw input, and unsupported
  transformed coordinates unchanged;
- exposes the two Windows settings plus runtime status and reset diagnostics;
- includes an independent reference model, exhaustive settings tests,
  real-action motion scenarios, sanitizers, a fuzzer, trace replay, a native
  benchmark, and a live synthetic-input integration test;
- ships CMake presets, install rules, adapter metadata, and Linux/Windows CI.

yes, version zero took the scenic route.
