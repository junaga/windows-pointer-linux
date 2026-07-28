#!/bin/sh
set -eu

gadget_root=/sys/kernel/config/usb_gadget
windows_name=windows-pointer-lab-windows
linux_name=windows-pointer-lab-linux
state_dir=/run/windows-pointer-lab
state_file=$state_dir/gadgets.env

require_root() {
    if [ "$(id -u)" -ne 0 ]; then
        printf 'run this command as root\n' >&2
        exit 1
    fi
}

write_report_descriptor() {
    # Eight buttons followed by signed 16-bit relative X and Y.
    printf '\005\001\011\002\241\001\011\001\241\000\005\011\031\001\051\010\025\000\045\001\225\010\165\001\201\002\005\001\011\060\011\061\026\000\200\046\377\177\165\020\225\002\201\006\300\300'
}

new_hidg() {
    before=$1
    attempts=0
    while [ "$attempts" -lt 40 ]; do
        for node in /dev/hidg*; do
            [ -e "$node" ] || continue
            case "
$before
" in
                *"
$node
"*) ;;
                *) printf '%s\n' "$node"; return 0 ;;
            esac
        done
        attempts=$((attempts + 1))
        sleep 0.05
    done
    return 1
}

wait_for_usb_driver() {
    product=$1
    attempts=0
    while [ "$attempts" -lt 80 ]; do
        for device in /sys/bus/usb/devices/*; do
            [ -f "$device/idVendor" ] || continue
            [ -f "$device/idProduct" ] || continue
            [ "$(cat "$device/idVendor")" = "1d6b" ] || continue
            [ "$(cat "$device/idProduct")" = "$product" ] || continue
            for interface in "$device":*; do
                [ -L "$interface/driver" ] || continue
                [ "$(basename "$(readlink -f "$interface/driver")")" = "usbhid" ] || continue
                return 0
            done
        done
        attempts=$((attempts + 1))
        sleep 0.05
    done
    return 1
}

create_gadget() {
    name=$1
    product=$2
    serial=$3
    udc=$4
    gadget=$gadget_root/$name

    [ ! -e "$gadget" ] || {
        printf 'gadget already exists: %s\n' "$gadget" >&2
        return 1
    }

    before=$(find /dev -maxdepth 1 -name 'hidg*' -print 2>/dev/null || true)
    mkdir "$gadget"
    printf '0x1d6b' > "$gadget/idVendor"
    printf '%s' "$product" > "$gadget/idProduct"
    printf '0x0200' > "$gadget/bcdUSB"
    printf '0x0100' > "$gadget/bcdDevice"

    mkdir "$gadget/strings/0x409"
    printf '%s' "$serial" > "$gadget/strings/0x409/serialnumber"
    printf 'windows-pointer-linux' > "$gadget/strings/0x409/manufacturer"
    printf 'pointer lab relative mouse' > "$gadget/strings/0x409/product"

    mkdir "$gadget/configs/c.1"
    mkdir "$gadget/configs/c.1/strings/0x409"
    printf 'deterministic HID replay' > "$gadget/configs/c.1/strings/0x409/configuration"
    printf '120' > "$gadget/configs/c.1/MaxPower"

    mkdir "$gadget/functions/hid.usb0"
    printf '2' > "$gadget/functions/hid.usb0/protocol"
    printf '1' > "$gadget/functions/hid.usb0/subclass"
    printf '5' > "$gadget/functions/hid.usb0/report_length"
    write_report_descriptor > "$gadget/functions/hid.usb0/report_desc"
    ln -s "$gadget/functions/hid.usb0" "$gadget/configs/c.1/hid.usb0"
    printf '%s' "$udc" > "$gadget/UDC"

    node=$(new_hidg "$before") || {
        printf 'no /dev/hidg endpoint appeared for %s\n' "$name" >&2
        return 1
    }
    product_number=$(printf '%s' "$product" | sed 's/^0x//')
    wait_for_usb_driver "$product_number" || {
        printf 'USB HID driver did not bind to %s\n' "$name" >&2
        return 1
    }
    chmod 0660 "$node"
    if [ -n "${SUDO_GID:-}" ]; then
        chown "root:$SUDO_GID" "$node"
    fi
    printf '%s\n' "$node"
}

remove_gadget() {
    name=$1
    gadget=$gadget_root/$name
    [ -d "$gadget" ] || return 0

    printf '' > "$gadget/UDC"
    rm -f "$gadget/configs/c.1/hid.usb0"
    rmdir "$gadget/functions/hid.usb0"
    rmdir "$gadget/configs/c.1/strings/0x409"
    rmdir "$gadget/configs/c.1"
    rmdir "$gadget/strings/0x409"
    rmdir "$gadget"
}

setup() {
    require_root
    modprobe dummy_hcd num=2
    modprobe libcomposite
    modprobe usb_f_hid

    udcs=$(find /sys/class/udc -maxdepth 1 -mindepth 1 -printf '%f\n' | sort)
    windows_udc=$(printf '%s\n' "$udcs" | sed -n '1p')
    linux_udc=$(printf '%s\n' "$udcs" | sed -n '2p')
    [ -n "$windows_udc" ] && [ -n "$linux_udc" ] || {
        printf 'two USB device controllers are required\n' >&2
        exit 1
    }

    windows_hidg=$(create_gadget "$windows_name" 0x1050 WPLAB-WINDOWS "$windows_udc")
    linux_hidg=$(create_gadget "$linux_name" 0x1051 WPLAB-LINUX "$linux_udc")

    mkdir -p "$state_dir"
    {
        printf 'WINDOWS_HIDG=%s\n' "$windows_hidg"
        printf 'WINDOWS_USB_ID=1d6b:1050\n'
        printf 'WINDOWS_SERIAL=WPLAB-WINDOWS\n'
        printf 'LINUX_HIDG=%s\n' "$linux_hidg"
        printf 'LINUX_USB_ID=1d6b:1051\n'
        printf 'LINUX_SERIAL=WPLAB-LINUX\n'
    } > "$state_file"
    chmod 0644 "$state_file"
    status
}

teardown() {
    require_root
    remove_gadget "$windows_name"
    remove_gadget "$linux_name"
    rm -f "$state_file"
    rmdir "$state_dir" 2>/dev/null || true
}

status() {
    for name in "$windows_name" "$linux_name"; do
        gadget=$gadget_root/$name
        if [ -d "$gadget" ]; then
            serial=$(cat "$gadget/strings/0x409/serialnumber")
            product=$(cat "$gadget/idProduct")
            udc=$(cat "$gadget/UDC")
            printf '%s: serial=%s product=%s udc=%s\n' "$name" "$serial" "$product" "$udc"
        else
            printf '%s: absent\n' "$name"
        fi
    done
    [ ! -f "$state_file" ] || cat "$state_file"
    lsusb -d 1d6b:1050 2>/dev/null || true
    lsusb -d 1d6b:1051 2>/dev/null || true
}

case "${1:-}" in
    setup) setup ;;
    teardown) teardown ;;
    status) status ;;
    *)
        printf 'usage: %s setup|status|teardown\n' "$0" >&2
        exit 2
        ;;
esac
