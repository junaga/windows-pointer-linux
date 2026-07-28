#!/bin/sh
set -eu

export XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-/run/user/$(id -u)}

if [ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ]; then
    HYPRLAND_INSTANCE_SIGNATURE=$(
        hyprctl instances -j |
            jq -er 'max_by(.time) | .instance'
    )
    export HYPRLAND_INSTANCE_SIGNATURE
fi

exec hyprctl "$@"
