#!/bin/sh
set -eu

if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

device="${IIO_DEVICE:-/sys/bus/iio/devices/iio:device0}"
channel="${1:-4}"
interval="${2:-0.2}"
sample_count="${3:-0}"
bar_width="${4:-40}"
raw_file="$device/in_voltage${channel}_raw"
scale_file="$device/in_voltage_scale"

case "$channel" in
    ''|*[!0-9]*)
        printf '错误：通道必须是非负整数。\n' >&2
        exit 1
        ;;
esac

case "$sample_count" in
    ''|*[!0-9]*)
        printf '错误：采样次数必须是非负整数，0 表示持续读取。\n' >&2
        exit 1
        ;;
esac

case "$bar_width" in
    ''|*[!0-9]*)
        printf '错误：条形图宽度必须是正整数。\n' >&2
        exit 1
        ;;
esac

if [ "$bar_width" -lt 1 ]; then
    printf '错误：条形图宽度必须大于 0。\n' >&2
    exit 1
fi

if [ ! -r "$raw_file" ] || [ ! -r "$scale_file" ]; then
    printf '错误：找不到 ADC 通道或 scale：%s\n' "$device" >&2
    exit 1
fi

scale="$(cat "$scale_file")"
printf 'ADC channel=%s scale=%s; press Ctrl+C to stop\n' "$channel" "$scale"

trap 'printf "\n"; exit 0' HUP INT TERM

i=0
while :; do
    raw="$(cat "$raw_file")"
    display="$(awk -v raw="$raw" -v scale="$scale" -v width="$bar_width" '
        BEGIN {
            maximum = 8191
            value = raw + 0
            if (value < 0) value = 0
            if (value > maximum) value = maximum

            percent = value * 100 / maximum
            filled = int(value * width / maximum + 0.5)

            printf "ADC %6.2f%% [", percent
            for (i = 0; i < width; i++) {
                printf "%s", (i < filled ? "#" : "-")
            }
            printf "] raw=%-5d voltage=%7.3f mV", raw, raw * scale
        }
    ')"
    printf '\r\033[K%s' "$display"
    i=$((i + 1))
    if [ "$sample_count" -gt 0 ] && [ "$i" -ge "$sample_count" ]; then
        break
    fi
    sleep "$interval"
done

printf '\n'
