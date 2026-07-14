#!/bin/sh
set -eu

if (set -o pipefail) 2>/dev/null; then
    set -o pipefail
fi

led="${LED_PATH:-/sys/class/leds/work}"
count="${1:-3}"
interval="${2:-0.3}"

case "$count" in
    ''|*[!0-9]*)
        printf '错误：闪烁次数必须是正整数。\n' >&2
        exit 1
        ;;
esac

if [ "$count" -lt 1 ]; then
    printf '错误：闪烁次数必须大于 0。\n' >&2
    exit 1
fi

if [ ! -d "$led" ] || [ ! -w "$led/brightness" ] || [ ! -w "$led/trigger" ]; then
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
}

trap restore_led EXIT HUP INT TERM

printf 'LED=%s original_trigger=%s\n' "$led" "${original_trigger:-unknown}"
printf 'none\n' > "$led/trigger"

i=1
while [ "$i" -le "$count" ]; do
    printf '1\n' > "$led/brightness"
    sleep "$interval"
    printf '0\n' > "$led/brightness"
    sleep "$interval"
    printf 'blink=%s/%s\n' "$i" "$count"
    i=$((i + 1))
done

printf 'done; restoring trigger=%s\n' "${original_trigger:-unknown}"
