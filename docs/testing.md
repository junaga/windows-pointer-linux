# testing a mouse without trusting vibes

The motion engine is deliberately independent from Hyprland. This keeps the
interesting arithmetic testable on Linux, Windows, and in CI without starting
a compositor every time a remainder changes by one.

## normal tests

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The suite covers:

- all 20 Windows pointer-speed positions, with EPP on and off;
- display regions at 96, 120, 144, 192, and 480 DPI;
- segment transitions, signed remainders, zero reports, setting changes, and
  the full signed 16-bit device-count range;
- three very scientific traces: answering a Discord call, fighting somebody
  in Hypixel, and dragging a file into a website.

`tests/reference_model.hpp` is a straight-line implementation of the audited
Windows arithmetic. It shares public types and constants with the production
engine only where copying them would make the test less readable. It does not
call production engine internals.

## sanitizers and fuzzing

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

For longer abuse, use Clang's libFuzzer:

```sh
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DWINDOWS_POINTER_BUILD_PLUGIN=OFF \
  -DWINDOWS_POINTER_BUILD_FUZZER=ON \
  -DBUILD_TESTING=OFF
cmake --build build/fuzz
./build/fuzz/windows_pointer_fuzzer
```

## replaying traces

The native replay tool accepts raw integer mouse reports on standard input:

```sh
./build/dev/windows-pointer-replay \
  --speed 10/20 --epp on --dpi 96 \
  < tests/traces/discord-call.csv
```

Its CSV output contains both the input and accelerated result. That makes it
easy to diff traces without inventing another file format because apparently
commas have survived this long.

## asking Windows

On Windows, the build also produces `windows-pointer-windows-oracle`. It saves
the current desktop mouse settings and cursor position, changes the two public
Windows settings, injects each report with `SendInput`, observes the cursor,
then restores everything even if the run fails:

```powershell
Get-Content tests\traces\discord-call.csv |
  build\dev\windows-pointer-windows-oracle.exe --speed 10 --epp on
```

Run it on an otherwise idle desktop. The Windows pointer accumulator and
previous curve segment are session-global private state. There is no public API
that resets either one, so the first few observed pixels can depend on motion
that happened before the tool started. This oracle is useful for whole-trace
comparison and spotting algorithm changes; it is not dishonest enough to
claim control over private state that Windows does not expose.

The tool reports the system DPI it observed. For per-monitor comparisons, put
the cursor on the target display before starting it and keep the trace inside
that display.

## asking the running compositor

With a development build and a Hyprland session:

```sh
scripts/live-test.sh build/dev
```

The script creates a temporary `/dev/uinput` mouse, sends one raw report
through libinput and Hyprland, and checks the plugin's per-device diagnostics.
It needs write access to `/dev/uinput`; a normal desktop logind ACL is enough.
The device is always destroyed when the helper exits.

The diagnostics are also useful without the script:

```sh
hyprctl windows-pointer-linux
hyprctl -j windows-pointer-linux
hyprctl windows-pointer-linux reset
```

`reset` clears the session motion state and counters. The live test verifies
that the entire input path reaches the engine. Exact arithmetic stays in the
isolated engine tests: moving another physical mouse during a live run changes
the deliberately shared Windows accumulator, so comparing one injected report
against a fresh engine would itself be the wrong model.

## benchmark

```sh
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/windows_pointer_benchmark
```

The benchmark prints steady-state motion and a deliberately absurd case that
rebuilds the curve by crossing a DPI boundary on every report. Numbers are
machine-specific; the distinction between those paths is the useful part.
