#!/bin/sh
set -eu

if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

device="${1:-/sys/bus/iio/devices/iio:device0}"

if [ ! -d "$device" ]; then
    printf '错误：找不到 IIO ADC 设备：%s\n' "$device" >&2
    exit 1
fi

name="unknown"
if [ -r "$device/name" ]; then
    name="$(cat "$device/name")"
fi

if [ ! -r "$device/in_voltage_scale" ]; then
    printf '错误：找不到 ADC scale：%s/in_voltage_scale\n' "$device" >&2
    exit 1
fi

scale="$(cat "$device/in_voltage_scale")"

printf 'device=%s\n' "$device"
printf 'name=%s\n' "$name"
printf 'scale=%s\n' "$scale"
printf '%-10s %-10s %-14s\n' 'channel' 'raw' 'estimated_mV'

found=0
for raw_file in "$device"/in_voltage*_raw; do
    if [ ! -r "$raw_file" ]; then
        continue
    fi

    found=1
    file_name="${raw_file##*/}"
    channel="${file_name#in_voltage}"
    channel="${channel%_raw}"
    raw="$(cat "$raw_file")"
    estimated_mv="$(awk -v raw="$raw" -v scale="$scale" 'BEGIN { printf "%.3f", raw * scale }')"
    printf '%-10s %-10s %-14s\n' "$channel" "$raw" "$estimated_mv"
done

if [ "$found" -eq 0 ]; then
    printf '错误：设备中没有可读的 in_voltage*_raw。\n' >&2
    exit 1
fi
