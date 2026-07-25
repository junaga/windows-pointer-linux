# testing

Pointer behavior is unusually easy to judge and unusually hard to verify.
“Feels right” remains useful acceptance feedback. It is not a conformance
test, especially after changing one signed remainder at 2 a.m.

The test strategy separates five questions:

1. Does the production engine match the audited algorithm?
2. Does it preserve state across realistic report sequences?
3. Is the integer implementation safe across its complete input range?
4. Does a desktop adapter deliver real reports to the engine?
5. Is the pointer path cheap enough to run on every event?

## conformance model

`tests/reference_model.hpp` is a straight-line implementation of the audited
Windows arithmetic. It does not call production engine internals. Public
types and documented constants are shared only where duplicating them would
make the test less independent, not more correct.

The model comparison covers:

- all 20 pointer-speed positions;
- EPP enabled and disabled;
- 96, 120, 144, 192, and 480 DPI display regions;
- thousands of deterministic reports per configuration;
- positive, negative, axial, diagonal, tiny, and large motion;
- segment transitions and the one-report upward transition rule;
- signed enhanced and linear subpixel remainders;
- zero reports;
- setting and display-region changes;
- signed 16-bit device limits.

Run the normal suite with:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

CI builds the platform-independent engine and tests with GCC, Clang, and MSVC.
The desktop adapter is built and exercised locally against the exact running
compositor headers because its ABI is intentionally version-specific.

## state and scenario regressions

Focused unit tests make ownership rules explicit:

- every engine has independent session state;
- one engine preserves state across reports;
- linear and enhanced accumulators survive EPP changes independently;
- configuration changes do not silently reset motion;
- only an explicit reset clears state;
- zero reports do not erase the previous curve segment;
- current-display DPI selects the active curve.

Scenario traces exercise longer sequences named after actual pointer work:

- answering a Discord call without missing the tiny button;
- playing Hypixel PvP with extremely normal arm movements;
- dragging a file into a website and releasing it over the drop target.

The names are human. The assertions are still exact integer output and final
position.

## sanitizers

AddressSanitizer and UndefinedBehaviorSanitizer cover the production engine,
reference model, and regression scenarios:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

Separate boundary tests drive both signs of the complete 32-bit event range
through the production engine and verify defined, clamped output. This matters
because the algorithm uses several fixed-point intermediate formats. Passing
the happy-path trace is not permission to overflow on a malformed event.

## fuzzing

Clang's libFuzzer drives arbitrary initial settings, display DPIs, and
32-bit motion reports through the engine:

```sh
cmake -S . -B build/fuzz -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++ \
  -DWINDOWS_POINTER_BUILD_PLUGIN=OFF \
  -DWINDOWS_POINTER_BUILD_FUZZER=ON \
  -DBUILD_TESTING=OFF
cmake --build build/fuzz
./build/fuzz/windows_pointer_fuzzer
```

Fuzzing checks memory and integer safety. The deterministic reference-model
suite remains the semantic oracle.

## trace replay

The native replay tool accepts raw integer reports on standard input:

```sh
./build/dev/windows-pointer-replay \
  --speed 10/20 --epp on --dpi 96 \
  < tests/traces/discord-call.csv
```

Its CSV output contains every input and accelerated result. Traces can be
captured, reviewed, and diffed without introducing a bespoke binary format
that only its author remembers three weeks later.

## adapter integration

The shipped adapter has an end-to-end Linux test:

```sh
scripts/live-test.sh build/dev
```

The script creates a temporary `/dev/uinput` mouse, sends a raw report through
uinput, libinput, and a running Hyprland session, then verifies the adapter's
per-device diagnostics. It needs write access to `/dev/uinput`; a normal
desktop logind ACL is sufficient. The helper destroys the temporary device on
exit.

Runtime diagnostics are available with:

```sh
hyprctl windows-pointer-linux
hyprctl -j windows-pointer-linux
hyprctl windows-pointer-linux reset
```

The integration test proves that a real report reaches the engine through the
complete adapter path. Exact arithmetic remains in the isolated engine tests.
Another physical mouse can move during a live test and deliberately shares the
same Windows session state, so treating one injected report as a fresh engine
would test the wrong architecture.

Future adapters should provide an equivalent synthetic end-to-end test for
their own input path.

## benchmark

```sh
cmake --preset benchmark
cmake --build --preset benchmark
./build/benchmark/windows_pointer_benchmark
```

The benchmark reports steady-state motion and an intentionally hostile case
that crosses a DPI boundary on every report, forcing curve reconstruction.
Absolute numbers are machine-specific. The separation between steady state
and rebuild cost is the useful result.
