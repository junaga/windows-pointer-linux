#!/usr/bin/env bash
set -euo pipefail

LAB_ROOT=${WINDOWS_POINTER_LAB_ROOT:-/usr/local/var/lib/windows-pointer-lab}
SSH_KEY="${LAB_ROOT}/state/ssh/id_ed25519"
LINUX_NAME=wplab-linux
WINDOWS_NAME=wplab-windows
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
TRACE=${1:-"${REPO_ROOT}/tests/traces/discord-call.csv"}
INTERVAL_MS=${INTERVAL_MS:-20}
RUN_ID=$(date -u +%Y%m%dT%H%M%SZ)
OUTPUT="${REPO_ROOT}/build/lab/dual-vm-${RUN_ID}"
report_count=$(
    awk 'NF && $0 !~ /^#/ {count++} END {print count+0}' "${TRACE}"
)
capture_seconds=$(
    awk -v reports="${report_count}" -v interval="${INTERVAL_MS}" \
        'BEGIN {print int((reports * interval + 999) / 1000) + 3}'
)

guest_ip() {
    virsh --connect qemu:///system domifaddr "$1" --source lease |
        awk '/ipv4/ {sub("/.*", "", $4); print $4; exit}'
}

ssh_linux() {
    local address=$1
    shift
    ssh -i "${SSH_KEY}" \
        -o BatchMode=yes \
        -o ConnectTimeout=5 \
        -o StrictHostKeyChecking=accept-new \
        "lab@${address}" "$@"
}

windows_run() {
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${windows_address}" run "$1"
}

windows_wait_file() {
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${windows_address}" \
        wait-file "$1" --timeout 120
}

linux_address=$(guest_ip "${LINUX_NAME}")
windows_address=$(guest_ip "${WINDOWS_NAME}")
if [[ -z "${linux_address}" || -z "${windows_address}" ]]; then
    echo "both guests must have DHCP addresses" >&2
    exit 1
fi

install -d "${OUTPUT}/windows"
windows_vnc=$("${SCRIPT_DIR}/vm-lab.sh" vnc "${WINDOWS_NAME}")

linux_hyprctl="~/pointer-lab/guest-hyprctl.sh"
plugin="~/pointer-lab/windows-pointer-linux.so"
linux_plugin_loaded=false
digital_attached=false

cleanup() {
    if ${linux_plugin_loaded}; then
        ssh_linux "${linux_address}" \
            "${linux_hyprctl} windows-pointer-linux trace stop >/dev/null 2>&1 || true;
             ${linux_hyprctl} plugin unload ${plugin} >/dev/null 2>&1 || true" \
            || true
    fi
    if ${digital_attached}; then
        "${SCRIPT_DIR}/vm-lab.sh" detach-digital >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

"${SCRIPT_DIR}/vm-lab.sh" attach-digital
digital_attached=true

ssh_linux "${linux_address}" \
    "${linux_hyprctl} plugin load ${plugin};
     ${linux_hyprctl} windows-pointer-linux trace start"
linux_plugin_loaded=true

windows_run \
    "Remove-Item 'C:\\pointer-lab\\capture' -Recurse -Force -ErrorAction SilentlyContinue;
     Remove-Item 'C:\\pointer-lab\\ready.txt' -Force -ErrorAction SilentlyContinue"
virsh --connect qemu:///system send-key \
    "${WINDOWS_NAME}" KEY_LEFTMETA KEY_R
sleep 1
vncdo -t 10 --delay=1 -s "${windows_vnc}" \
    type powershell \
    key enter \
    pause 4 \
    type "cd /pointer-lab" \
    key enter \
    pause 1 \
    type "./windows-pointer-windows-capture.exe --output capture --ready-file ready.txt --seconds ${capture_seconds}" \
    key enter
windows_wait_file "C:\pointer-lab\ready.txt"

sudo python3 "${SCRIPT_DIR}/replay-hid.py" \
    --from-state /run/windows-pointer-lab/gadgets.env \
    --interval-ms "${INTERVAL_MS}" \
    --output "${OUTPUT}/host-replay.csv" \
    "${TRACE}"

ssh_linux "${linux_address}" \
    "${linux_hyprctl} windows-pointer-linux trace stop"
ssh_linux "${linux_address}" \
    "${linux_hyprctl} windows-pointer-linux trace dump" \
    >"${OUTPUT}/linux.csv"

windows_wait_file "C:\pointer-lab\capture\metadata.txt"
install -d "${OUTPUT}/windows/capture"
for file in metadata.txt raw.csv pointer.csv; do
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${windows_address}" get \
        "C:\\pointer-lab\\capture\\${file}" \
        "${OUTPUT}/windows/capture/${file}"
done

python3 "${SCRIPT_DIR}/windows-capture-to-common.py" \
    "${OUTPUT}/windows/capture" "${OUTPUT}/windows.csv"
set +e
python3 "${SCRIPT_DIR}/compare-captures.py" \
    "${OUTPUT}/windows.csv" "${OUTPUT}/linux.csv"
comparison_status=$?
set -e
python3 "${SCRIPT_DIR}/analyze-capture-timing.py" \
    "${OUTPUT}/host-replay.csv" \
    "${OUTPUT}/windows.csv" \
    "${OUTPUT}/linux.csv"

ssh_linux "${linux_address}" \
    "${linux_hyprctl} plugin unload ${plugin}"
linux_plugin_loaded=false
"${SCRIPT_DIR}/vm-lab.sh" detach-digital
digital_attached=false
trap - EXIT

echo "dual-VM capture: ${OUTPUT}"
exit "${comparison_status}"
