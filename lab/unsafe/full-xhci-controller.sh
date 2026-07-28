#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd -- "${SCRIPT_DIR}/.." && pwd)
XHCI_XML="${SCRIPT_DIR}/xhci-hostdev.xml"
PCI_ADDRESS=0000:00:14.0
RECOVERY_UNIT=windows-pointer-lab-controller-recovery
RECOVERY_MINUTES=20
DOMAINS=(wplab-windows wplab-linux)

usage() {
    echo "usage: sudo $0 <attach-windows|attach-linux|restore-host|status>"
    echo
    echo "UNSAFE: this takes every USB device on the host xHCI controller."
    echo "Use lab/bluetooth-radio.sh with a dedicated USB adapter instead."
}

require_root() {
    if [[ ${EUID} -ne 0 ]]; then
        echo "run this command with sudo" >&2
        exit 1
    fi
}

domain_running() {
    [[ $(virsh --connect qemu:///system domstate "$1" 2>/dev/null) == running ]]
}

controller_in_domain() {
    local domain=$1
    virsh --connect qemu:///system dumpxml "${domain}" 2>/dev/null |
        xmllint --xpath \
            "boolean(/domain/devices/hostdev[@type='pci']/source/address[@domain='0x0000' and @bus='0x00' and @slot='0x14' and @function='0x0'])" \
            - 2>/dev/null |
        rg -q true
}

arm_recovery() {
    systemctl stop "${RECOVERY_UNIT}.timer" >/dev/null 2>&1 || true
    systemctl reset-failed "${RECOVERY_UNIT}.service" \
        "${RECOVERY_UNIT}.timer" >/dev/null 2>&1 || true
    systemd-run \
        --quiet \
        --unit="${RECOVERY_UNIT}" \
        --on-active="${RECOVERY_MINUTES}m" \
        --timer-property=AccuracySec=1s \
        --property=Type=oneshot \
        "$(realpath "$0")" restore-host --from-timer
}

cancel_recovery() {
    systemctl stop "${RECOVERY_UNIT}.timer" >/dev/null 2>&1 || true
}

verify_isolation() {
    local group_path device_count
    group_path=$(readlink -f "/sys/bus/pci/devices/${PCI_ADDRESS}/iommu_group")
    device_count=$(find "${group_path}/devices" -mindepth 1 -maxdepth 1 | wc -l)
    if [[ ${device_count} -ne 1 ||
        ! -e "${group_path}/devices/${PCI_ADDRESS}" ]]; then
        echo "${PCI_ADDRESS} is not isolated in its IOMMU group" >&2
        find "${group_path}/devices" -mindepth 1 -maxdepth 1 -printf '%f\n' >&2
        exit 1
    fi
}

restore_host() {
    local from_timer=${1:-}
    for domain in "${DOMAINS[@]}"; do
        if domain_running "${domain}" && controller_in_domain "${domain}"; then
            virsh --connect qemu:///system detach-device \
                "${domain}" "${XHCI_XML}" --live || true
        fi
    done
    modprobe xhci_pci
    modprobe btusb
    systemctl start bluetooth.service
    if [[ ${from_timer} != --from-timer ]]; then
        cancel_recovery
    fi
    udevadm settle
    echo "physical xHCI controller restored to the host"
}

attach_controller() {
    local domain=$1
    verify_isolation
    if ! domain_running "${domain}"; then
        echo "${domain} is not running" >&2
        exit 1
    fi
    for candidate in "${DOMAINS[@]}"; do
        if domain_running "${candidate}" && controller_in_domain "${candidate}"; then
            if [[ ${candidate} == "${domain}" ]]; then
                echo "${PCI_ADDRESS} is already attached to ${domain}"
                arm_recovery
                return
            fi
            echo "${PCI_ADDRESS} is already attached to ${candidate}" >&2
            exit 1
        fi
    done

    arm_recovery
    systemctl stop bluetooth.service
    if ! virsh --connect qemu:///system attach-device \
        "${domain}" "${XHCI_XML}" --live; then
        restore_host
        exit 1
    fi
    echo "physical xHCI controller attached to ${domain}"
    echo "automatic host restoration armed for ${RECOVERY_MINUTES} minutes"
}

status() {
    local owner=host
    for domain in "${DOMAINS[@]}"; do
        if domain_running "${domain}" && controller_in_domain "${domain}"; then
            owner=${domain}
        fi
    done
    echo "controller_owner=${owner}"
    echo "host_driver=$(basename "$(readlink -f "/sys/bus/pci/devices/${PCI_ADDRESS}/driver" 2>/dev/null || echo none)")"
    if systemctl is-active --quiet "${RECOVERY_UNIT}.timer"; then
        systemctl show "${RECOVERY_UNIT}.timer" \
            --property=ActiveState \
            --property=NextElapseUSecRealtime
    else
        echo "recovery_timer=inactive"
    fi
}

require_root
if [[ ${1:-} == attach-windows || ${1:-} == attach-linux ]]; then
    if [[ ${WINDOWS_POINTER_ALLOW_FULL_XHCI_TAKEOVER:-} != yes ]]; then
        echo "refusing full host USB-controller takeover" >&2
        echo "this disconnects the host keyboard, mouse, webcam, and Bluetooth" >&2
        echo "use ${LAB_DIR}/bluetooth-radio.sh with a dedicated adapter" >&2
        echo "expert override: WINDOWS_POINTER_ALLOW_FULL_XHCI_TAKEOVER=yes" >&2
        exit 1
    fi
fi
case ${1:-} in
    attach-windows)
        attach_controller wplab-windows
        ;;
    attach-linux)
        attach_controller wplab-linux
        ;;
    restore-host)
        restore_host "${2:-}"
        ;;
    status)
        status
        ;;
    *)
        usage
        exit 2
        ;;
esac
