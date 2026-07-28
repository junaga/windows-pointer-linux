#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
TEMPLATE="${SCRIPT_DIR}/host/bluetooth-usb.xml.in"
RUNTIME_DIR=/run/windows-pointer-lab
WINDOWS_NAME=wplab-windows
LINUX_NAME=wplab-linux

usage() {
    echo "usage: sudo $0 <status|attach-windows|attach-linux|detach> [USB_PATH]"
    echo
    echo "USB_PATH is a stable sysfs name such as 1-12."
    echo "Without USB_PATH, attach chooses the only idle Bluetooth USB adapter."
    echo "An adapter carrying a connected host device is never selected."
}

require_root() {
    if [[ ${EUID} -ne 0 ]]; then
        echo "run this command with sudo" >&2
        exit 1
    fi
}

hci_usb_path() {
    local hci=$1 current
    current=$(readlink -f "/sys/class/bluetooth/${hci}/device")
    while [[ ${current} != / ]]; do
        if [[ -f "${current}/idVendor" && -f "${current}/idProduct" &&
            -f "${current}/busnum" && -f "${current}/devnum" ]]; then
            basename "${current}"
            return
        fi
        current=$(dirname "${current}")
    done
    return 1
}

hci_for_usb() {
    local requested hci mapped
    requested=$(basename "$1")
    for hci in /sys/class/bluetooth/hci*; do
        [[ -e "${hci}" ]] || continue
        [[ $(basename "${hci}") =~ ^hci[0-9]+$ ]] || continue
        mapped=$(hci_usb_path "$(basename "${hci}")") || continue
        if [[ ${mapped} == "${requested}" ]]; then
            basename "${hci}"
            return
        fi
    done
    return 1
}

hci_has_connection() {
    local hci=$1 object value
    while IFS= read -r object; do
        [[ ${object} == "/org/bluez/${hci}/dev_"* ]] || continue
        value=$(
            busctl get-property \
                org.bluez "${object}" org.bluez.Device1 Connected \
                2>/dev/null || true
        )
        [[ ${value} == "b true" ]] && return 0
    done < <(busctl tree --list org.bluez 2>/dev/null || true)
    return 1
}

bluetooth_usb_paths() {
    local hci
    for hci in /sys/class/bluetooth/hci*; do
        [[ -e "${hci}" ]] || continue
        [[ $(basename "${hci}") =~ ^hci[0-9]+$ ]] || continue
        hci_usb_path "$(basename "${hci}")"
    done | sort -u
}

describe_path() {
    local name=$1 path hci vendor product bus device safety description
    path="/sys/bus/usb/devices/${name}"
    [[ -d "${path}" ]] || return 1
    hci=$(hci_for_usb "${path}") || return 1
    vendor=$(<"${path}/idVendor")
    product=$(<"${path}/idProduct")
    bus=$(<"${path}/busnum")
    device=$(<"${path}/devnum")
    safety=idle
    hci_has_connection "${hci}" && safety=HOST-IN-USE
    description=$(lsusb -s "${bus}:${device}" | sed -E 's/^Bus [^ ]+ Device [^ ]+: ID [^ ]+ //')
    printf '%-8s %s:%s %-5s %-11s %s\n' \
        "${name}" "${vendor}" "${product}" "${hci}" "${safety}" "${description}"
}

select_idle_path() {
    local requested=${1:-} name hci
    if [[ -n "${requested}" ]]; then
        [[ -d "/sys/bus/usb/devices/${requested}" ]] || {
            echo "Bluetooth USB path does not exist: ${requested}" >&2
            exit 1
        }
        hci=$(hci_for_usb "/sys/bus/usb/devices/${requested}") || {
            echo "${requested} is not a Bluetooth USB adapter" >&2
            exit 1
        }
        if hci_has_connection "${hci}"; then
            echo "refusing ${requested}: ${hci} carries a connected host device" >&2
            echo "attach a dedicated USB Bluetooth adapter instead" >&2
            exit 1
        fi
        echo "${requested}"
        return
    fi

    local -a idle=()
    while IFS= read -r name; do
        hci=$(hci_for_usb "/sys/bus/usb/devices/${name}")
        hci_has_connection "${hci}" || idle+=("${name}")
    done < <(bluetooth_usb_paths)
    if [[ ${#idle[@]} -ne 1 ]]; then
        echo "expected exactly one idle Bluetooth USB adapter; found ${#idle[@]}" >&2
        echo "connect a dedicated adapter or pass its USB_PATH explicitly" >&2
        exit 1
    fi
    echo "${idle[0]}"
}

domain_running() {
    [[ $(virsh --connect qemu:///system domstate "$1" 2>/dev/null) == running ]]
}

detach_all() {
    local state domain
    shopt -s nullglob
    for state in "${RUNTIME_DIR}"/bluetooth-radio-*.xml; do
        domain=${state##*/bluetooth-radio-}
        domain=${domain%.xml}
        if domain_running "${domain}"; then
            virsh --connect qemu:///system detach-device \
                "${domain}" "${state}" --live || true
        fi
        rm -f -- "${state}"
    done
    shopt -u nullglob
    udevadm settle
    echo "dedicated Bluetooth USB passthrough detached"
}

attach_radio() {
    local domain=$1 requested=${2:-} name path hci vendor product bus device state
    domain_running "${domain}" || {
        echo "${domain} must be running" >&2
        exit 1
    }
    detach_all >/dev/null
    name=$(select_idle_path "${requested}")
    path="/sys/bus/usb/devices/${name}"
    hci=$(hci_for_usb "${path}")
    vendor=$(<"${path}/idVendor")
    product=$(<"${path}/idProduct")
    bus=$(<"${path}/busnum")
    device=$(<"${path}/devnum")
    install -d -m 0755 "${RUNTIME_DIR}"
    state="${RUNTIME_DIR}/bluetooth-radio-${domain}.xml"
    sed \
        -e "s/@VENDOR@/${vendor}/g" \
        -e "s/@PRODUCT@/${product}/g" \
        -e "s/@BUS@/${bus}/g" \
        -e "s/@DEVICE@/${device}/g" \
        "${TEMPLATE}" >"${state}"
    virsh --connect qemu:///system attach-device "${domain}" "${state}" --live
    echo "attached only Bluetooth USB ${name} (${vendor}:${product}, ${hci}) to ${domain}"
    echo "host keyboard, non-Bluetooth mice, webcam, and other USB devices remain attached"
}

status() {
    local found=false name
    printf '%-8s %-9s %-5s %-11s %s\n' \
        PATH USB_ID HCI SAFETY DESCRIPTION
    while IFS= read -r name; do
        found=true
        describe_path "${name}"
    done < <(bluetooth_usb_paths)
    ${found} || echo "no host Bluetooth USB adapters found"
    shopt -s nullglob
    local state
    for state in "${RUNTIME_DIR}"/bluetooth-radio-*.xml; do
        echo "active_passthrough=${state##*/bluetooth-radio-}"
    done
    shopt -u nullglob
}

require_root
case ${1:-} in
    status)
        status
        ;;
    attach-windows)
        attach_radio "${WINDOWS_NAME}" "${2:-}"
        ;;
    attach-linux)
        attach_radio "${LINUX_NAME}" "${2:-}"
        ;;
    detach)
        detach_all
        ;;
    *)
        usage
        exit 2
        ;;
esac
