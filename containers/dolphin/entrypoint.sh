#!/usr/bin/env bash
set -euo pipefail

display="${DOLPHIN_DISPLAY:-:99}"
display_number="${display#:}"
display_number="${display_number%%.*}"
xorg_template="${DOLPHIN_XORG_TEMPLATE:-/opt/wii-arena/etc/xorg.conf.template}"
xorg_conf="${DOLPHIN_XORG_CONF:-/tmp/wii-arena-xorg.conf}"
xorg_log="${DOLPHIN_XORG_LOG:-/tmp/wii-arena-xorg.log}"
x_socket="/tmp/.X11-unix/X${display_number}"

mkdir -p /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix

bus_id() {
    local pci bus device_function device func
    pci="$(nvidia-smi --query-gpu=pci.bus_id --format=csv,noheader)"
    pci="${pci%%$'\n'*}"
    pci="${pci//[[:space:]]/}"
    pci="${pci#*:}"
    bus="${pci%%:*}"
    device_function="${pci#*:}"
    device="${device_function%%.*}"
    func="${device_function#*.}"
    printf 'PCI:%d:%d:%d' "$((16#${bus}))" "$((16#${device}))" "$((10#${func}))"
}

if [ -f "${xorg_template}" ]; then
    sed "s|@BUS_ID@|$(bus_id)|" "${xorg_template}" > "${xorg_conf}"
    xorg_config_args=(-config "${xorg_conf}")
else
    xorg_config_args=()
fi

Xorg "${display}" \
    -noreset \
    -nolisten tcp \
    "${xorg_config_args[@]}" \
    -logfile "${xorg_log}" &
xorg_pid="$!"
app_pid=""

cleanup() {
    if [ -n "${app_pid}" ] && kill -0 "${app_pid}" 2>/dev/null; then
        kill "${app_pid}" 2>/dev/null || true
        wait "${app_pid}" 2>/dev/null || true
    fi
    if kill -0 "${xorg_pid}" 2>/dev/null; then
        kill "${xorg_pid}" 2>/dev/null || true
        wait "${xorg_pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

for _ in $(seq 1 100); do
    if [ -S "${x_socket}" ]; then
        if command -v xdpyinfo >/dev/null 2>&1; then
            if DISPLAY="${display}" xdpyinfo >/dev/null 2>&1; then
                break
            fi
        else
            break
        fi
    fi
    if ! kill -0 "${xorg_pid}" 2>/dev/null; then
        echo "Xorg exited before ${display} became ready." >&2
        cat "${xorg_log}" >&2 || true
        exit 1
    fi
    sleep 0.1
done

if ! kill -0 "${xorg_pid}" 2>/dev/null; then
    echo "Xorg is not running." >&2
    cat "${xorg_log}" >&2 || true
    exit 1
fi
if [ ! -S "${x_socket}" ]; then
    echo "Timed out waiting for Xorg socket ${x_socket}." >&2
    cat "${xorg_log}" >&2 || true
    exit 1
fi

export DISPLAY="${display}"
"$@" &
app_pid="$!"

set +e
wait "${app_pid}"
status="$?"
app_pid=""
exit "${status}"