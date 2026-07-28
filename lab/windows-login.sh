#!/usr/bin/env bash
set -euo pipefail

LAB_ROOT=${WINDOWS_POINTER_LAB_ROOT:-/usr/local/var/lib/windows-pointer-lab}
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
WINDOWS_NAME=wplab-windows
AUTOUNATTEND="${SCRIPT_DIR}/guest/windows/Autounattend.xml"

windows_ip() {
    virsh --connect qemu:///system domifaddr "${WINDOWS_NAME}" --source lease |
        awk '/ipv4/ {sub("/.*", "", $4); print $4; exit}'
}

windows_ready() {
    local address
    address=$(windows_ip 2>/dev/null || true)
    [[ -n "${address}" ]] &&
        nc -z -w 1 "${address}" 5985 >/dev/null 2>&1 &&
        timeout 8 python3 "${SCRIPT_DIR}/windows-remote.py" \
            --host "${address}" ping >/dev/null 2>&1
}

send_character() {
    local character=$1 upper
    case ${character} in
        [A-Z])
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" KEY_LEFTSHIFT "KEY_${character}" >/dev/null
            ;;
        [a-z])
            upper=${character^^}
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" "KEY_${upper}" >/dev/null
            ;;
        [0-9])
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" "KEY_${character}" >/dev/null
            ;;
        '!')
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" KEY_LEFTSHIFT KEY_1 >/dev/null
            ;;
        '@')
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" KEY_LEFTSHIFT KEY_2 >/dev/null
            ;;
        '#')
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" KEY_LEFTSHIFT KEY_3 >/dev/null
            ;;
        '$')
            virsh --connect qemu:///system send-key \
                "${WINDOWS_NAME}" KEY_LEFTSHIFT KEY_4 >/dev/null
            ;;
        *)
            echo "unsupported disposable lab-password character" >&2
            exit 1
            ;;
    esac
}

type_password() {
    local password index
    password=$(
        xmllint --xpath \
            "string(//*[local-name()='LocalAccount']/*[local-name()='Password']/*[local-name()='Value'])" \
            "${AUTOUNATTEND}"
    )
    [[ -n "${password}" ]] || {
        echo "could not read the disposable lab password" >&2
        exit 1
    }
    for ((index = 0; index < ${#password}; index++)); do
        send_character "${password:index:1}"
    done
    virsh --connect qemu:///system send-key \
        "${WINDOWS_NAME}" KEY_ENTER >/dev/null
}

[[ $(virsh --connect qemu:///system domstate "${WINDOWS_NAME}" 2>/dev/null) == running ]] || {
    echo "${WINDOWS_NAME} is not running" >&2
    exit 1
}
windows_ready && {
    echo "Windows remote control is already ready"
    exit 0
}

endpoint=$("${SCRIPT_DIR}/vm-lab.sh" vnc "${WINDOWS_NAME}")
echo "opening the disposable Windows lab sign-in"
vncdo -t 10 -s "${endpoint}" key enter pause 2
type_password
for _ in $(seq 1 30); do
    sleep 2
    windows_ready && {
        echo "Windows interactive session is ready"
        exit 0
    }
done

# If Enter initially submitted an empty password, the first attempt only
# dismissed the resulting dialog. Type once more into the credential field.
echo "retrying the disposable Windows lab sign-in"
vncdo -t 10 -s "${endpoint}" key enter pause 1
type_password
for _ in $(seq 1 30); do
    sleep 2
    windows_ready && {
        echo "Windows interactive session is ready"
        exit 0
    }
done

echo "Windows did not become remotely ready after automated sign-in" >&2
exit 1
