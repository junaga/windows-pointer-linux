# contributing, somehow

Keep the motion engine independent from Hyprland. An input algorithm that can
only be tested by wiggling a real mouse is performance art.

Before a change:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

cmake --preset sanitize
cmake --build --preset sanitize
ctest --preset sanitize
```

Changes to fixed-point arithmetic need a focused regression test and must still
match `tests/reference_model.hpp`. Changes based on a new Windows build should
record the exact file versions and hashes in
`docs/research/windows-11.md`. Do not commit Microsoft binaries, PDBs, registry
exports containing personal data, or a large pile of “seems faster now.”

Plugin changes should also pass:

```sh
scripts/live-test.sh build/dev
```

Unload the plugin before rebuilding the same `.so` in place. Hyprland executes
it inside the compositor process; overwriting mapped code is a bold testing
strategy.

See `docs/testing.md` for the replay, Windows oracle, fuzzer, and benchmark.
Small commits are preferred. Lowercase sarcasm is welcome. Unexplained magic
numbers are not.
