#!/bin/sh
set -eu

if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

led="${LED_PATH:-/sys/class/leds/work}"
device="${IIO_DEVICE:-/sys/bus/iio/devices/iio:device0}"
channel="${ADC_CHANNEL:-4}"
cycle_limit="${1:-0}"
min_delay="${MIN_DELAY_SECONDS:-0.10}"
max_delay="${MAX_DELAY_SECONDS:-1.50}"
raw_file="$device/in_voltage${channel}_raw"

case "$cycle_limit" in
    ''|*[!0-9]*)
        printf '错误：闪烁次数必须是非负整数，0 表示持续运行。\n' >&2
        exit 1
        ;;
esac

if [ ! -r "$raw_file" ]; then
    printf '错误：找不到 ADC 通道：%s\n' "$raw_file" >&2
    exit 1
fi

if [ ! -w "$led/trigger" ] || [ ! -w "$led/brightness" ]; then
    printf '错误：找不到可写的 LED 接口：%s\n' "$led" >&2
    exit 1
fi

original_trigger="$(sed -n 's/.*\[\([^]]*\)\].*/\1/p' "$led/trigger")"
original_brightness="$(cat "$led/brightness")"

restore_led() {
    if [ -n "$original_trigger" ]; then
        printf '%s\n' "$original_trigger" > "$led/trigger"
    fi
    if [ "$original_trigger" = 'none' ]; then
        printf '%s\n' "$original_brightness" > "$led/brightness"
    fi
    printf '已恢复 LED：trigger=%s brightness=%s\n' \
        "${original_trigger:-unknown}" "$original_brightness"
}

trap restore_led EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

printf 'none\n' > "$led/trigger"
printf 'ADC 通道 %s 控制 LED 速度：数值越大，闪烁越快。按 Ctrl+C 停止。\n' "$channel"

cycle=0
while :; do
    raw="$(cat "$raw_file")"
    metrics="$(awk \
        -v raw="$raw" \
        -v minimum="$min_delay" \
        -v maximum="$max_delay" '
        BEGIN {
            full_scale = 8191
            value = raw + 0
            if (value < 0) value = 0
            if (value > full_scale) value = full_scale

            percent = value * 100 / full_scale
            delay = maximum - (maximum - minimum) * value / full_scale
            printf "%.1f %.3f", percent, delay
        }
    ')"
    percent="${metrics%% *}"
    delay="${metrics#* }"

    cycle=$((cycle + 1))
    printf 'cycle=%-4s raw=%-5s adc=%5s%% half_period=%ss\n' \
        "$cycle" "$raw" "$percent" "$delay"

    printf '1\n' > "$led/brightness"
    sleep "$delay"
    printf '0\n' > "$led/brightness"
    sleep "$delay"

    if [ "$cycle_limit" -gt 0 ] && [ "$cycle" -ge "$cycle_limit" ]; then
        break
    fi
done
