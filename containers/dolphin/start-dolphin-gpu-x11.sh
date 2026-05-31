#!/usr/bin/env bash
set -euo pipefail

display="${DOLPHIN_DISPLAY:-:99}"
display_number="${display#:}"
display_number="${display_number%%.*}"
xorg_conf="${DOLPHIN_XORG_CONF:-/tmp/wii-arena-xorg.conf}"
xorg_log="${DOLPHIN_XORG_LOG:-/tmp/wii-arena-xorg.log}"
x_socket="/tmp/.X11-unix/X${display_number}"

mkdir -p /tmp/.X11-unix
chmod 1777 /tmp/.X11-unix

cat >"${xorg_conf}" <<EOF
Section "ServerLayout"
    Identifier "wii-arena"
    Screen 0 "Screen0"
EndSection

Section "ServerFlags"
    Option "AllowMouseOpenFail" "true"
    Option "DontVTSwitch" "true"
EndSection

Section "Files"
    ModulePath "/usr/lib/x86_64-linux-gnu/nvidia/xorg"
    ModulePath "/usr/lib/xorg/modules"
EndSection

Section "Device"
    Identifier "Device0"
    Driver "nvidia"
    Option "AllowEmptyInitialConfiguration" "true"
    Option "UseDisplayDevice" "none"
    Option "HardDPMS" "false"
EndSection

Section "Screen"
    Identifier "Screen0"
    Device "Device0"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Virtual 1280 720
    EndSubSection
EndSection
EOF

Xorg "${display}" \
    -noreset \
    -nolisten tcp \
    -config "${xorg_conf}" \
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
