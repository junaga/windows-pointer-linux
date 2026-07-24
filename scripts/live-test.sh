#!/bin/sh
set -eu

build_dir=${1:-build/dev}
plugin="$build_dir/windows-pointer-linux.so"
uinput="$build_dir/windows-pointer-uinput"
trace="30,12"

if ! hyprctl plugin list | grep -q "Plugin windows-pointer-linux "; then
    hyprctl plugin load "$(realpath "$plugin")" >/dev/null
fi

hyprctl windows-pointer-linux reset >/dev/null
"$uinput" $trace

status=$(hyprctl -j windows-pointer-linux)
test_device=$(
    printf '%s' "$status" |
        jq -c '.devices[] | select(.name == "windows-pointer-linux test mouse")'
)
processed=$(printf '%s' "$test_device" | jq -r '.processed')
last_raw=$(printf '%s' "$test_device" | jq -r '.raw | join(",")')
actual_output=$(printf '%s' "$test_device" | jq -r '.output | join(",")')

if [ "$processed" -ne 1 ]; then
    printf 'expected one processed report, got %s\n' "$processed" >&2
    exit 1
fi

if [ "$last_raw" != "$trace" ]; then
    printf 'expected raw report %s, got %s\n' "$trace" "$last_raw" >&2
    exit 1
fi

if [ -z "$actual_output" ] || [ "$actual_output" = "null" ]; then
    printf 'the hook processed the report but forgot to produce output, impressive\n' >&2
    exit 1
fi

printf '%s\n' "$status" | jq .
