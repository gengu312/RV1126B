#!/bin/sh
set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
output_dir="$repo_root/build/pressure-gpio"

mkdir -p "$output_dir"
aarch64-linux-gnu-gcc \
    -std=c11 \
    -O2 \
    -Wall \
    -Wextra \
    "$script_dir/pressure_gpio.c" \
    -o "$output_dir/pressure-gpio"
aarch64-linux-gnu-strip --strip-unneeded "$output_dir/pressure-gpio"
printf 'Built %s\n' "$output_dir/pressure-gpio"
