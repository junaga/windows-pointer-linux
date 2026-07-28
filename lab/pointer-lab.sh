#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)
LAB_ROOT=${WINDOWS_POINTER_LAB_ROOT:-/usr/local/var/lib/windows-pointer-lab}
WINDOWS_NAME=wplab-windows
LINUX_NAME=wplab-linux

usage() {
    echo "usage: $0 <start|test|status|stop|screenshot> [arguments]"
    echo
    echo "  start                  start and prepare both friendly VMs"
    echo "  test [TRACE]           run the fully automated dual-VM test"
    echo "  status                 show VM, synthetic HID, and Bluetooth safety state"
    echo "  stop                   clean up synthetic devices and stop both VMs"
    echo "  screenshot VM [FILE]   capture windows or linux without opening a viewer"
}

guest_ip() {
    virsh --connect qemu:///system domifaddr "$1" --source lease 2>/dev/null |
        awk '/ipv4/ {sub("/.*", "", $4); print $4; exit}'
}

wait_for_windows_network() {
    local address
    for _ in $(seq 1 90); do
        address=$(guest_ip "${WINDOWS_NAME}")
        if [[ -n "${address}" ]] &&
            nc -z -w 1 "${address}" 5985 >/dev/null 2>&1; then
            return
        fi
        sleep 1
    done
    return 1
}

prepare_windows() {
    local address
    if ! wait_for_windows_network; then
        "${SCRIPT_DIR}/windows-login.sh"
    fi
    "${SCRIPT_DIR}/vm-lab.sh" wait-windows
    address=$("${SCRIPT_DIR}/vm-lab.sh" windows-ip)
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${address}" put \
        "${SCRIPT_DIR}/guest/windows/Console.ps1" \
        'C:\pointer-lab\Console.ps1'
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${address}" put \
        "${SCRIPT_DIR}/guest/windows/Repair-PointerLab.ps1" \
        'C:\pointer-lab\Repair-PointerLab.ps1'
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${address}" run \
        "powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\pointer-lab\Repair-PointerLab.ps1"
}

start_lab() {
    python3 "${SCRIPT_DIR}/preflight.py"
    "${SCRIPT_DIR}/vm-lab.sh" start
    "${SCRIPT_DIR}/vm-lab.sh" wait-linux
    prepare_windows
    "${SCRIPT_DIR}/vm-lab.sh" sync-linux
    "${SCRIPT_DIR}/vm-lab.sh" sync-windows
    echo
    echo "Pointer Lab ready:"
    echo "  Linux:  $(guest_ip "${LINUX_NAME}")"
    echo "  Windows: $(guest_ip "${WINDOWS_NAME}")"
    echo "  Safe automated test: ${SCRIPT_DIR}/pointer-lab.sh test"
}

run_test() {
    local trace=${1:-}
    cmake --build "${REPO_ROOT}/build/dev"
    cmake --build "${REPO_ROOT}/build/windows-capture"
    start_lab
    if [[ -n "${trace}" ]]; then
        "${SCRIPT_DIR}/run-digital-test.sh" "${trace}"
    else
        "${SCRIPT_DIR}/run-digital-test.sh"
    fi
}

show_status() {
    python3 "${SCRIPT_DIR}/preflight.py" || true
    echo
    "${SCRIPT_DIR}/vm-lab.sh" status
    echo
    sudo "${SCRIPT_DIR}/bluetooth-radio.sh" status
    echo
    if [[ -L /sys/bus/pci/devices/0000:00:14.0/driver ]]; then
        echo "host_usb_controller_driver=$(basename "$(readlink -f /sys/bus/pci/devices/0000:00:14.0/driver)")"
    fi
}

stop_lab() {
    sudo "${SCRIPT_DIR}/bluetooth-radio.sh" detach || true
    "${SCRIPT_DIR}/vm-lab.sh" detach-digital || true
    "${SCRIPT_DIR}/vm-lab.sh" stop
}

take_screenshot() {
    local requested=${1:-} domain output endpoint
    case ${requested} in
        windows) domain=${WINDOWS_NAME} ;;
        linux) domain=${LINUX_NAME} ;;
        *)
            echo "screenshot VM must be windows or linux" >&2
            exit 2
            ;;
    esac
    output=${2:-"${REPO_ROOT}/build/lab/${requested}-screen.png"}
    install -d "$(dirname "${output}")"
    endpoint=$("${SCRIPT_DIR}/vm-lab.sh" vnc "${domain}")
    vncdo -t 10 -s "${endpoint}" capture "${output}"
    echo "${output}"
}

case ${1:-} in
    start)
        start_lab
        ;;
    test)
        run_test "${2:-}"
        ;;
    status)
        show_status
        ;;
    stop)
        stop_lab
        ;;
    screenshot)
        take_screenshot "${2:-}" "${3:-}"
        ;;
    *)
        usage
        exit 2
        ;;
esac
