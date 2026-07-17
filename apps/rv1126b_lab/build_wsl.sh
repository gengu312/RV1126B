#!/bin/sh
set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
repo_root="$(CDPATH= cd -- "$script_dir/../.." && pwd)"
build_dir="$repo_root/build/rv1126b-lab"
lib_dir="$repo_root/build/board-sysroot/usr/lib"
output="$build_dir/rv1126blab"
qt_include="/usr/include/x86_64-linux-gnu/qt5"

for command_name in \
    aarch64-linux-gnu-g++ \
    aarch64-linux-gnu-objdump \
    aarch64-linux-gnu-strip
do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        printf 'Missing command: %s\n' "$command_name" >&2
        exit 1
    fi
done

for library in \
    libQt5Widgets.so.5.15.11 \
    libQt5Gui.so.5.15.11 \
    libQt5Core.so.5.15.11
do
    if [ ! -f "$lib_dir/$library" ]; then
        printf 'Missing board Qt library: %s\n' "$lib_dir/$library" >&2
        printf 'Run apps/rv1126b_lab/prepare_sysroot.ps1 in PowerShell first.\n' >&2
        exit 1
    fi
done

mkdir -p "$build_dir"

aarch64-linux-gnu-g++ \
    -std=c++17 \
    -O2 \
    -pipe \
    -fPIC \
    -fno-exceptions \
    -Wall \
    -Wextra \
    -I"$qt_include" \
    -I"$qt_include/QtCore" \
    -I"$qt_include/QtGui" \
    -I"$qt_include/QtWidgets" \
    "$script_dir/main.cpp" \
    "$lib_dir/libQt5Widgets.so.5.15.11" \
    "$lib_dir/libQt5Gui.so.5.15.11" \
    "$lib_dir/libQt5Core.so.5.15.11" \
    -Wl,--allow-shlib-undefined \
    -Wl,-rpath-link,"$lib_dir" \
    -pthread \
    -o "$output"

aarch64-linux-gnu-strip --strip-unneeded "$output"
if aarch64-linux-gnu-objdump -T "$output" \
    | grep -Eq '\(CXXABI_1\.3\.(1[5-9]|[2-9][0-9]+)\)'
then
    printf 'Binary requires a CXXABI newer than the board maximum 1.3.14.\n' >&2
    exit 1
fi
printf 'Built %s\n' "$output"
