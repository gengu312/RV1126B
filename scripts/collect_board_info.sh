#!/bin/sh
set -eu

if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

section() {
    printf '\n===== %s =====\n' "$1"
}

run_shell() {
    label="$1"
    command_text="$2"
    printf '\n--- %s ---\n' "$label"
    if sh -c "$command_text" 2>&1; then
        :
    else
        status=$?
        printf '[跳过或失败，退出码 %s]\n' "$status"
    fi
}

printf 'RV1126B board information report\n'
printf 'generated_at='
date '+%Y-%m-%dT%H:%M:%S%z' 2>/dev/null || date

section 'identity'
run_shell 'device-tree model' "tr -d '\\000' < /proc/device-tree/model; echo"
run_shell 'device-tree compatible' "tr '\\000' '\\n' < /proc/device-tree/compatible"
run_shell 'uname' 'uname -a'
run_shell 'os-release' 'cat /etc/os-release'
run_shell 'kernel command line' 'cat /proc/cmdline'

section 'cpu memory storage'
run_shell 'cpuinfo' 'cat /proc/cpuinfo'
run_shell 'meminfo' 'cat /proc/meminfo'
run_shell 'filesystems' 'df -h 2>/dev/null || df'
run_shell 'mounts' 'cat /proc/mounts'

section 'network'
run_shell 'addresses' 'if command -v ip >/dev/null 2>&1; then ip addr; else ifconfig -a; fi'
run_shell 'routes' 'if command -v ip >/dev/null 2>&1; then ip route; else route -n; fi'

section 'gpio'
run_shell 'gpio character devices' 'ls -l /dev/gpiochip* 2>/dev/null'
run_shell 'gpio sysfs' 'ls -la /sys/class/gpio 2>/dev/null'
run_shell 'gpio debug state' 'cat /sys/kernel/debug/gpio 2>/dev/null'
run_shell 'gpio tools' 'for c in gpiodetect gpioinfo gpioget gpioset; do p=$(command -v "$c" 2>/dev/null || true); [ -n "$p" ] && echo "$c=$p" || echo "$c=MISSING"; done'

section 'adc iio'
run_shell 'iio devices' 'ls -la /sys/bus/iio/devices 2>/dev/null'
run_shell 'adc values' 'for d in /sys/bus/iio/devices/iio:device*; do [ -d "$d" ] || continue; echo "DEVICE=$d"; [ -r "$d/name" ] && cat "$d/name"; for f in "$d"/in_voltage*; do [ -r "$f" ] || continue; printf "%s=" "$f"; cat "$f"; done; done'

section 'audio'
run_shell 'alsa cards' 'cat /proc/asound/cards 2>/dev/null'
run_shell 'capture devices' 'command -v arecord >/dev/null 2>&1 && arecord -l'
run_shell 'playback devices' 'command -v aplay >/dev/null 2>&1 && aplay -l'
run_shell 'sound nodes' 'ls -l /dev/snd 2>/dev/null'

section 'display'
run_shell 'display nodes' 'ls -l /dev/fb* /dev/dri/* 2>/dev/null'
run_shell 'framebuffer attributes' 'for f in /sys/class/graphics/fb*/name /sys/class/graphics/fb*/virtual_size /sys/class/graphics/fb*/bits_per_pixel; do [ -r "$f" ] && echo "$f=$(cat "$f")"; done'
run_shell 'drm connector status' 'for f in /sys/class/drm/card*-*/status /sys/class/drm/card*-*/modes; do [ -r "$f" ] && echo "$f=$(cat "$f")"; done'

section 'camera video'
run_shell 'media and video nodes' 'ls -l /dev/media* /dev/video* 2>/dev/null'
run_shell 'media tools' 'for c in media-ctl v4l2-ctl gst-launch-1.0 ffmpeg; do p=$(command -v "$c" 2>/dev/null || true); [ -n "$p" ] && echo "$c=$p" || echo "$c=MISSING"; done'

section 'development tools'
run_shell 'tool locations' 'for c in gcc g++ cc cmake make python3; do p=$(command -v "$c" 2>/dev/null || true); [ -n "$p" ] && echo "$c=$p" || echo "$c=MISSING"; done'

section 'relevant kernel messages'
run_shell 'gpio adc audio display camera npu' 'dmesg 2>/dev/null | grep -Ei "gpio|saradc|iio|es8390|es8389|audio|drm|dsi|camera|imx|csi|isp|rknn|npu" | tail -300'
