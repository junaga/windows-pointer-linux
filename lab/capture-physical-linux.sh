#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
DURATION=${1:-12}
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
OUTPUT=${2:-"${REPO_ROOT}/build/lab/physical-linux-${RUN_ID}"}
PLUGIN="${REPO_ROOT}/build/dev/windows-pointer-linux.so"

if ! [[ "${DURATION}" =~ ^[0-9]+$ ]] || ((DURATION < 1)); then
    echo "duration must be a positive integer" >&2
    exit 2
fi
if [[ ! -f "${PLUGIN}" ]]; then
    echo "missing plugin build: ${PLUGIN}" >&2
    exit 1
fi

mouse_event=""
for candidate in /dev/input/event*; do
    properties=$(udevadm info -q property "${candidate}" 2>/dev/null || true)
    if rg -q '^ID_BUS=bluetooth$' <<<"${properties}" &&
        rg -q '^ID_INPUT_MOUSE=1$' <<<"${properties}"; then
        mouse_event=${candidate}
        break
    fi
done
if [[ -z "${mouse_event}" ]]; then
    echo "no Bluetooth mouse input device found" >&2
    exit 1
fi
bluetooth_address=$(
    bluetoothctl devices Connected |
        awk '/Pebble M350s/ {print $2; exit}'
)

install -d "${OUTPUT}"
plugin_was_loaded=false
plugin_loaded_here=false
capture_pids=()

if hyprctl plugin list | rg -q 'windows-pointer-linux'; then
    plugin_was_loaded=true
else
    hyprctl plugin load "${PLUGIN}"
    plugin_loaded_here=true
fi

cleanup() {
    for process in "${capture_pids[@]}"; do
        kill -INT "${process}" >/dev/null 2>&1 || true
    done
    hyprctl windows-pointer-linux trace stop >/dev/null 2>&1 || true
    hyprctl windows-pointer-linux trace dump >"${OUTPUT}/plugin.csv" 2>/dev/null ||
        true
    if ${plugin_loaded_here}; then
        hyprctl plugin unload "${PLUGIN}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

{
    echo "duration_seconds=${DURATION}"
    echo "input_device=${mouse_event}"
    echo "plugin_previously_loaded=${plugin_was_loaded}"
    echo "kernel=$(uname -r)"
    if [[ -n "${bluetooth_address}" ]]; then
        bluetoothctl info "${bluetooth_address}"
    fi
    sudo libinput list-devices |
        sed -n '/Device:.*Pebble M350s/,/^$/p'
} >"${OUTPUT}/metadata.txt"

hyprctl windows-pointer-linux reset
hyprctl windows-pointer-linux trace start

sudo timeout --signal=INT --kill-after=2 "${DURATION}" \
    btmon -w "${OUTPUT}/bluetooth.btsnoop" \
    >"${OUTPUT}/btmon.stdout" 2>"${OUTPUT}/btmon.stderr" &
capture_pids+=("$!")

sudo timeout --signal=INT --kill-after=2 "${DURATION}" \
    evtest "${mouse_event}" \
    >"${OUTPUT}/evtest.txt" 2>&1 &
capture_pids+=("$!")

sudo timeout --signal=INT --kill-after=2 "${DURATION}" \
    stdbuf -oL libinput debug-events \
        --device "${mouse_event}" \
        --verbose \
    >"${OUTPUT}/libinput.txt" 2>&1 &
capture_pids+=("$!")

echo "physical Linux capture armed for ${DURATION} seconds"
for process in "${capture_pids[@]}"; do
    wait "${process}" || true
done
capture_pids=()

hyprctl windows-pointer-linux trace stop
hyprctl windows-pointer-linux trace dump >"${OUTPUT}/plugin.csv"
btmon -r "${OUTPUT}/bluetooth.btsnoop" >"${OUTPUT}/bluetooth.txt"

if ${plugin_loaded_here}; then
    hyprctl plugin unload "${PLUGIN}"
    plugin_loaded_here=false
fi
trap - EXIT

echo "physical Linux capture: ${OUTPUT}"
