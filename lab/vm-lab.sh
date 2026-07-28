#!/usr/bin/env bash
set -euo pipefail

LAB_ROOT=${WINDOWS_POINTER_LAB_ROOT:-/usr/local/var/lib/windows-pointer-lab}
IMAGE_DIR="${LAB_ROOT}/images"
STATE_DIR="${LAB_ROOT}/state"
VM_DIR="${LAB_ROOT}/vms"
SSH_DIR="${STATE_DIR}/ssh"
LINUX_NAME=wplab-linux
WINDOWS_NAME=wplab-windows
LINUX_BASE="${IMAGE_DIR}/debian-14-generic-amd64-20260724.qcow2"
LINUX_DISK="${VM_DIR}/${LINUX_NAME}.qcow2"
LINUX_SEED="${VM_DIR}/${LINUX_NAME}-seed.iso"
SSH_KEY="${SSH_DIR}/id_ed25519"
WINDOWS_TOOLS_ISO="${VM_DIR}/windows-pointer-tools.iso"
SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

usage() {
    echo "usage: $0 <command> [arguments]"
    echo
    echo "commands:"
    echo "  provision-linux             create and start the Debian/Hyprland guest"
    echo "  wait-linux                  wait for cloud-init and SSH"
    echo "  sync-linux                  copy the plugin and trace tools into Linux"
    echo "  provision-windows ISO       create and start a Windows 11 guest"
    echo "  start                       start both existing guests"
    echo "  stop                        gracefully stop both guests"
    echo "  wait-windows                wait for Windows setup and WinRM"
    echo "  sync-windows                copy current capture tools into Windows"
    echo "  attach-digital              attach one synthetic HID mouse to each guest"
    echo "  detach-digital              detach the synthetic mice"
    echo "  linux-ip                    print the Linux guest IPv4 address"
    echo "  windows-ip                  print the Windows guest IPv4 address"
    echo "  vnc DOMAIN                  print a vncdo-compatible endpoint"
    echo "  status                      show domains, addresses, and USB gadgets"
}

ensure_state_dirs() {
    local lab_user lab_group
    lab_user=$(id -un)
    lab_group=$(id -gn)
    sudo install -d -m 0755 -o "${lab_user}" -g "${lab_group}" \
        "${STATE_DIR}" "${VM_DIR}" "${SSH_DIR}"
}

domain_exists() {
    virsh --connect qemu:///system dominfo "$1" >/dev/null 2>&1
}

domain_running() {
    [[ $(virsh --connect qemu:///system domstate "$1" 2>/dev/null) == running ]]
}

start_all() {
    local domain
    for domain in "${LINUX_NAME}" "${WINDOWS_NAME}"; do
        if ! domain_exists "${domain}"; then
            echo "missing VM: ${domain}" >&2
            exit 1
        fi
        if domain_running "${domain}"; then
            echo "${domain} already running"
        else
            virsh --connect qemu:///system start "${domain}"
        fi
    done
}

stop_all() {
    local domain deadline all_stopped
    for domain in "${WINDOWS_NAME}" "${LINUX_NAME}"; do
        if domain_running "${domain}"; then
            virsh --connect qemu:///system shutdown "${domain}"
        fi
    done
    deadline=$((SECONDS + 90))
    while ((SECONDS < deadline)); do
        all_stopped=true
        for domain in "${WINDOWS_NAME}" "${LINUX_NAME}"; do
            domain_running "${domain}" && all_stopped=false
        done
        ${all_stopped} && {
            echo "both pointer-lab VMs are stopped"
            return
        }
        sleep 2
    done
    echo "VM shutdown is still in progress; no forced power-off was used" >&2
    return 1
}

vnc_endpoint() {
    local domain=$1 display host number
    domain_running "${domain}" || {
        echo "${domain} is not running" >&2
        exit 1
    }
    display=$(virsh --connect qemu:///system vncdisplay "${domain}")
    host=${display%:*}
    number=${display##*:}
    [[ ${number} =~ ^[0-9]+$ ]] || {
        echo "unexpected VNC display: ${display}" >&2
        exit 1
    }
    echo "${host}::$((5900 + number))"
}

ensure_ssh_key() {
    if [[ ! -f "${SSH_KEY}" ]]; then
        ssh-keygen -q -t ed25519 -N "" -f "${SSH_KEY}"
    fi
}

linux_ip() {
    local address
    address=$(
        virsh --connect qemu:///system domifaddr "${LINUX_NAME}" \
            --source lease 2>/dev/null |
        awk '/ipv4/ {sub("/.*", "", $4); print $4; exit}'
    )
    if [[ -z "${address}" ]]; then
        return 1
    fi
    echo "${address}"
}

windows_ip() {
    local address
    address=$(
        virsh --connect qemu:///system domifaddr "${WINDOWS_NAME}" \
            --source lease 2>/dev/null |
        awk '/ipv4/ {sub("/.*", "", $4); print $4; exit}'
    )
    if [[ -z "${address}" ]]; then
        return 1
    fi
    echo "${address}"
}

render_linux_user_data() {
    local public_key
    public_key=$(<"${SSH_KEY}.pub")
    sed "s|@SSH_PUBLIC_KEY@|${public_key}|" \
        "${SCRIPT_DIR}/guest/linux-user-data.yaml.in" \
        >"${STATE_DIR}/linux-user-data.yaml"
}

provision_linux() {
    ensure_state_dirs
    ensure_ssh_key
    if [[ ! -f "${LINUX_BASE}" ]]; then
        echo "missing verified Debian image: ${LINUX_BASE}" >&2
        exit 1
    fi
    if domain_exists "${LINUX_NAME}"; then
        echo "${LINUX_NAME} already exists"
        if ! domain_running "${LINUX_NAME}"; then
            virsh --connect qemu:///system start "${LINUX_NAME}"
        fi
        return
    fi

    render_linux_user_data

    qemu-img create -q -f qcow2 -F qcow2 -b "${LINUX_BASE}" \
        "${LINUX_DISK}" 24G
    cloud-localds "${LINUX_SEED}" \
        "${STATE_DIR}/linux-user-data.yaml" \
        "${SCRIPT_DIR}/guest/linux-meta-data.yaml"
    chmod 0644 "${LINUX_BASE}"
    chmod 0660 "${LINUX_DISK}" "${LINUX_SEED}"

    virt-install --connect qemu:///system \
        --name "${LINUX_NAME}" \
        --memory 2560 \
        --vcpus 2,cpuset=3-4 \
        --cpu host-passthrough \
        --machine q35 \
        --import \
        --disk "path=${LINUX_DISK},format=qcow2,bus=virtio" \
        --disk "path=${LINUX_SEED},device=cdrom,readonly=on" \
        --network network=default,model=virtio \
        --controller usb,model=qemu-xhci \
        --channel unix,target.type=virtio,target.name=org.qemu.guest_agent.0 \
        --graphics vnc,listen=127.0.0.1 \
        --video virtio \
        --rng /dev/urandom \
        --boot uefi \
        --osinfo detect=on,require=off \
        --noautoconsole
}

wait_linux() {
    local address=""
    ensure_state_dirs
    render_linux_user_data
    for _ in $(seq 1 120); do
        address=$(linux_ip || true)
        if [[ -n "${address}" ]] &&
            ssh -i "${SSH_KEY}" \
                -o BatchMode=yes \
                -o ConnectTimeout=3 \
                -o StrictHostKeyChecking=accept-new \
                "lab@${address}" \
                "test -f /var/lib/cloud/instance/boot-finished" \
                >/dev/null 2>&1; then
            scp -q -i "${SSH_KEY}" \
                -o StrictHostKeyChecking=accept-new \
                "${STATE_DIR}/linux-user-data.yaml" \
                "lab@${address}:/tmp/pointer-lab-user-data.yaml"
            ssh -i "${SSH_KEY}" \
                -o BatchMode=yes \
                -o StrictHostKeyChecking=accept-new \
                "lab@${address}" \
                "sudo cp /tmp/pointer-lab-user-data.yaml /var/lib/cloud/instance/cloud-config.txt &&
                 sudo cloud-init single --name write-files --frequency always &&
                 sudo chown -R lab:lab /home/lab &&
                 sudo rm -f /usr/sbin/policy-rc.d &&
                 sudo systemctl reset-failed greetd.service &&
                 sudo systemctl enable --now qemu-guest-agent.service ssh.service greetd.service"
            echo "${LINUX_NAME} is ready at ${address}"
            return
        fi
        sleep 5
    done
    echo "timed out waiting for ${LINUX_NAME}" >&2
    exit 1
}

sync_linux() {
    local address plugin
    address=$(linux_ip)
    plugin="${REPO_ROOT}/build/dev/windows-pointer-linux.so"
    if [[ ! -f "${plugin}" ]]; then
        echo "missing development plugin build: ${plugin}" >&2
        exit 1
    fi
    ssh -i "${SSH_KEY}" -o StrictHostKeyChecking=accept-new \
        "lab@${address}" \
        "sudo chown -R lab:lab /home/lab && install -d ~/pointer-lab"
    scp -i "${SSH_KEY}" -o StrictHostKeyChecking=accept-new \
        "${plugin}" \
        "${REPO_ROOT}/lab/compare-captures.py" \
        "${REPO_ROOT}/lab/guest/guest-hyprctl.sh" \
        "${REPO_ROOT}/tests/traces/discord-call.csv" \
        "lab@${address}:pointer-lab/"
    echo "Linux tools copied to lab@${address}:pointer-lab/"
}

sync_windows() {
    local address gateway server_pid transfer_status=0
    local collector="${REPO_ROOT}/build/windows-capture/windows-pointer-windows-capture.exe"
    ensure_state_dirs
    address=$(windows_ip)
    gateway=$(
        virsh --connect qemu:///system net-dumpxml default |
            sed -nE "s/.*<ip address='([^']+)'.*/\\1/p" |
            head -1
    )
    [[ -f "${collector}" ]] || {
        echo "missing Windows collector build: ${collector}" >&2
        exit 1
    }
    [[ -n "${gateway}" ]] || {
        echo "could not resolve the libvirt-private gateway" >&2
        exit 1
    }

    python3 -m http.server 18080 \
        --bind "${gateway}" \
        --directory "${REPO_ROOT}" \
        >"${STATE_DIR}/windows-sync-http.log" 2>&1 &
    server_pid=$!
    sleep 1
    python3 "${SCRIPT_DIR}/windows-remote.py" --host "${address}" run \
        "\$source = 'http://${gateway}:18080/build/windows-capture/windows-pointer-windows-capture.exe';
         \$temporary = 'C:\\pointer-lab\\windows-pointer-windows-capture.exe.new';
         Invoke-WebRequest -UseBasicParsing -Uri \$source -OutFile \$temporary;
         Move-Item \$temporary 'C:\\pointer-lab\\windows-pointer-windows-capture.exe' -Force" ||
        transfer_status=$?
    kill "${server_pid}" >/dev/null 2>&1 || true
    wait "${server_pid}" >/dev/null 2>&1 || true
    ((transfer_status == 0)) || return "${transfer_status}"

    for file in Console.ps1 Repair-PointerLab.ps1 Run-Capture.ps1; do
        python3 "${SCRIPT_DIR}/windows-remote.py" --host "${address}" put \
            "${SCRIPT_DIR}/guest/windows/${file}" \
            "C:\\pointer-lab\\${file}"
    done
    echo "Windows capture tools copied to ${address}:C:\\pointer-lab"
}

make_windows_tools_iso() {
    local collector="${REPO_ROOT}/build/windows-capture/windows-pointer-windows-capture.exe"
    local staging="${STATE_DIR}/windows-tools"
    if [[ ! -f "${collector}" ]]; then
        echo "missing Windows collector build: ${collector}" >&2
        exit 1
    fi
    install -d -m 0755 "${staging}"
    install -m 0644 \
        "${SCRIPT_DIR}/guest/windows/Autounattend.xml" \
        "${staging}/Autounattend.xml"
    install -m 0644 \
        "${SCRIPT_DIR}/guest/windows/Setup-PointerLab.ps1" \
        "${staging}/Setup-PointerLab.ps1"
    install -m 0644 \
        "${SCRIPT_DIR}/guest/windows/Run-Capture.ps1" \
        "${staging}/Run-Capture.ps1"
    install -m 0644 \
        "${SCRIPT_DIR}/guest/windows/Console.ps1" \
        "${staging}/Console.ps1"
    install -m 0644 \
        "${SCRIPT_DIR}/guest/windows/Repair-PointerLab.ps1" \
        "${staging}/Repair-PointerLab.ps1"
    install -m 0644 \
        "${collector}" \
        "${staging}/windows-pointer-windows-capture.exe"
    xorriso -as mkisofs -quiet -J -R -V WPLAB_TOOLS \
        -o "${WINDOWS_TOOLS_ISO}" "${staging}"
    chmod 0644 "${WINDOWS_TOOLS_ISO}"
}

provision_windows() {
    local iso=${1:-}
    ensure_state_dirs
    ensure_ssh_key
    if [[ -z "${iso}" || ! -f "${iso}" ]]; then
        echo "provision-windows requires a readable Windows 11 ISO path" >&2
        exit 1
    fi
    if domain_exists "${WINDOWS_NAME}"; then
        echo "${WINDOWS_NAME} already exists"
        if ! domain_running "${WINDOWS_NAME}"; then
            virsh --connect qemu:///system start "${WINDOWS_NAME}"
        fi
        return
    fi

    local windows_disk="${VM_DIR}/${WINDOWS_NAME}.qcow2"
    make_windows_tools_iso
    qemu-img create -q -f qcow2 "${windows_disk}" 64G
    chmod 0644 "${iso}"
    chmod 0660 "${windows_disk}"

    virt-install --connect qemu:///system \
        --name "${WINDOWS_NAME}" \
        --memory 4096 \
        --vcpus 2,cpuset=1-2 \
        --cpu host-passthrough \
        --machine q35 \
        --disk "path=${windows_disk},format=qcow2,bus=sata" \
        --disk "path=${iso},device=cdrom,readonly=on" \
        --disk "path=${WINDOWS_TOOLS_ISO},device=cdrom,readonly=on" \
        --network network=default,model=e1000e \
        --controller usb,model=qemu-xhci \
        --graphics vnc,listen=127.0.0.1 \
        --video qxl \
        --tpm backend.type=emulator,backend.version=2.0,model=tpm-crb \
        --boot uefi,firmware.feature0.name=secure-boot,firmware.feature0.enabled=yes,firmware.feature1.name=enrolled-keys,firmware.feature1.enabled=yes \
        --osinfo detect=on,require=off \
        --noautoconsole
}

wait_windows() {
    local address=""
    for _ in $(seq 1 240); do
        address=$(windows_ip || true)
        if [[ -n "${address}" ]] &&
            python3 "${SCRIPT_DIR}/windows-remote.py" \
                --host "${address}" ping >/dev/null 2>&1; then
            echo "${WINDOWS_NAME} is ready at ${address}"
            return
        fi
        sleep 5
    done
    echo "timed out waiting for ${WINDOWS_NAME}" >&2
    exit 1
}

usb_xml() {
    local product=$1
    local path=$2
    sed "s/@PRODUCT@/${product}/" >"${path}" <<'EOF'
<hostdev mode='subsystem' type='usb' managed='yes'>
  <source startupPolicy='optional'>
    <vendor id='0x1d6b'/>
    <product id='0x@PRODUCT@'/>
  </source>
</hostdev>
EOF
}

attach_one() {
    local domain=$1 product=$2 xml=$3
    if ! domain_exists "${domain}"; then
        echo "skipping absent domain ${domain}"
        return
    fi
    if ! domain_running "${domain}"; then
        echo "${domain} must be running before synthetic USB is attached" >&2
        exit 1
    fi
    if grep -q "<product id='0x${product}'" \
        < <(virsh --connect qemu:///system dumpxml "${domain}"); then
        echo "${domain} already has synthetic USB ${product}"
        return
    fi
    virsh --connect qemu:///system attach-device "${domain}" "${xml}" --live
}

detach_one() {
    local domain=$1 xml=$2 product
    if ! domain_exists "${domain}"; then
        return
    fi
    product=$(sed -nE "s/.*<product id='0x([^']+)'.*/\\1/p" "${xml}")
    if domain_running "${domain}" && [[ -n "${product}" ]] &&
        grep -q "<product id='0x${product}'" \
            < <(virsh --connect qemu:///system dumpxml "${domain}"); then
        virsh --connect qemu:///system detach-device "${domain}" "${xml}" \
            --live || true
    fi
    if [[ -n "${product}" ]] &&
        grep -q "<product id='0x${product}'" \
            < <(virsh --connect qemu:///system dumpxml "${domain}" --inactive); then
        virsh --connect qemu:///system detach-device "${domain}" "${xml}" \
            --config || true
    fi
}

attach_digital() {
    ensure_state_dirs
    if [[ ! -f /run/windows-pointer-lab/gadgets.env ]]; then
        sudo "${SCRIPT_DIR}/usb-gadgets.sh" setup
    fi
    local windows_xml="${STATE_DIR}/windows-hid.xml"
    local linux_xml="${STATE_DIR}/linux-hid.xml"
    usb_xml 1050 "${windows_xml}"
    usb_xml 1051 "${linux_xml}"
    attach_one "${WINDOWS_NAME}" 1050 "${windows_xml}"
    attach_one "${LINUX_NAME}" 1051 "${linux_xml}"
}

detach_digital() {
    local windows_xml="${STATE_DIR}/windows-hid.xml"
    local linux_xml="${STATE_DIR}/linux-hid.xml"
    [[ -f "${windows_xml}" ]] &&
        detach_one "${WINDOWS_NAME}" "${windows_xml}"
    [[ -f "${linux_xml}" ]] &&
        detach_one "${LINUX_NAME}" "${linux_xml}"
    sudo "${SCRIPT_DIR}/usb-gadgets.sh" teardown
}

status() {
    virsh --connect qemu:///system list --all
    echo
    for domain in "${LINUX_NAME}" "${WINDOWS_NAME}"; do
        if domain_exists "${domain}"; then
            if domain_running "${domain}"; then
                virsh --connect qemu:///system domifaddr "${domain}" \
                    --source lease || true
            else
                echo "${domain}: shut off"
            fi
        fi
    done
    echo
    sudo "${SCRIPT_DIR}/usb-gadgets.sh" status
}

case ${1:-} in
    provision-linux)
        provision_linux
        ;;
    wait-linux)
        wait_linux
        ;;
    sync-linux)
        sync_linux
        ;;
    sync-windows)
        sync_windows
        ;;
    provision-windows)
        provision_windows "${2:-}"
        ;;
    start)
        start_all
        ;;
    stop)
        stop_all
        ;;
    wait-windows)
        wait_windows
        ;;
    attach-digital)
        attach_digital
        ;;
    detach-digital)
        detach_digital
        ;;
    linux-ip)
        linux_ip
        ;;
    windows-ip)
        windows_ip
        ;;
    vnc)
        vnc_endpoint "${2:-}"
        ;;
    status)
        status
        ;;
    *)
        usage
        exit 2
        ;;
esac
