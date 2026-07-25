# contributing

Contributions are welcome. The one architectural rule is not negotiable:
keep the Windows engine independent from every desktop adapter.

An input algorithm that can only be tested by starting one compositor and
wiggling one mouse is performance art. The portable engine belongs in
`include/windows_pointer` and `src/engine.cpp`; platform interception belongs
in a separate adapter.

## build and test

The project requires CMake 3.25+, a C++23 compiler, and Ninja when using the
included presets.

For engine, tool, and test work on any supported platform:

```sh
cmake -S . -B build/engine -G Ninja \
  -DWINDOWS_POINTER_BUILD_PLUGIN=OFF \
  -DBUILD_TESTING=ON
cmake --build build/engine
ctest --test-dir build/engine --output-on-failure
```

On a Linux system with development headers matching the running Hyprland
build, the development preset also builds the shipped adapter:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Run the sanitizer configuration before submitting engine changes:

```sh
cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

The fuzzer, trace replay tool, benchmark, and live integration test are
documented in [testing](docs/testing.md).

## changing the engine

Changes to fixed-point arithmetic, curve construction, state transitions, or
slider mappings need:

- a focused regression test;
- agreement with `tests/reference_model.hpp`;
- sanitizer coverage;
- an explanation tied to the audited implementation record.

If a new Windows build changes the behavior, record its exact file version,
binary hash, PDB hash, relevant symbols, and observed difference in
[the Windows audit](docs/research/windows-11.md). Do not commit Microsoft
binaries, PDBs, registry exports containing personal data, or a folder called
`final-final-mouse-research`.

Keep platform types and headers out of the public engine API. A new engine
input is justified only when the audited Windows routine consumes the same
fact. Device DPI, polling rate, compositor velocity, and vibes do not qualify.

## changing an adapter

Adapter changes must preserve the contract in
[architecture](docs/architecture.md):

- one complete raw integer X/Y report enters the engine;
- one engine is shared per logical desktop pointer or seat;
- display DPI comes from the cursor's display region;
- only normal accelerated desktop motion is replaced;
- application raw input and unsupported device classes pass through.

The shipped adapter should additionally pass:

```sh
scripts/live-test.sh build/dev
```

Unload a locally loaded plugin before overwriting the same shared-object path.
The compositor executes that file in-process; replacing mapped code is not a
hot-reload protocol, however optimistic the shell prompt appears.

## adding an adapter

A new compositor or input-stack adapter should arrive with:

- a separate translation unit or subdirectory;
- no platform conditionals in the engine;
- documented build and configuration instructions;
- explicit device and raw-input scope;
- status diagnostics for processed and passed-through events;
- one synthetic end-to-end integration test;
- an architecture note only where its state ownership differs.

Use the smallest hook that exposes both the original relative report and the
desktop delta. If the platform cannot provide those facts, document the
missing boundary instead of approximating it silently.

## documentation and commits

Technical claims should name their evidence and boundary. General prose may be
human. Sarcasm is welcome; ambiguity in fixed-point math is not.

Prefer small commits that each explain one complete change. Write comments for
facts the code cannot express—audited constants, arithmetic differences, API
constraints—not for syntax standing directly below them.

Before opening a pull request, check:

- normal and sanitizer tests pass;
- changed links resolve;
- new settings or behavior are documented;
- no generated build files or proprietary audit inputs are included.
