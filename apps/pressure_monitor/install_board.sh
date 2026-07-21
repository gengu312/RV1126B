#!/bin/sh
set -eu

launcher_dir=/opt/ui/src/ATK-DLRV1126B
apps_dir=/opt/ui/src/apps
staged_binary=/tmp/pressuremonitor.new
entry_file=/tmp/pressuremonitor-entry.txt
remote_binary="$apps_dir/pressuremonitor"

cleanup()
{
    rm -f "$staged_binary" "$entry_file" /tmp/pressuremonitor-install.sh
    rm -f "$remote_binary.new"
    for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
        rm -f "$cfg.pressuremonitor-new"
    done
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$staged_binary" ] || [ ! -f "$entry_file" ]; then
    exit 10
fi

entry=$(awk 'NF == 3 && $3 == "pressuremonitor" { print; exit }' "$entry_file")
if [ -z "$entry" ]; then
    exit 20
fi

mkdir -p "$apps_dir"
for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
    if [ ! -f "$cfg" ]; then
        exit 10
    fi
    awk '$3 != "pressuremonitor" && NF { print }' "$cfg" \
        > "$cfg.pressuremonitor-new"
    if [ "$cfg" = "$launcher_dir/apk1.cfg" ]; then
        printf '%s\n' "$entry" >> "$cfg.pressuremonitor-new"
    fi
    awk 'NF != 3 { bad = 1 } END { exit bad }' "$cfg.pressuremonitor-new"
done

install -m 0755 "$staged_binary" "$remote_binary.new"
mv -f "$remote_binary.new" "$remote_binary"

for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
    if [ ! -f "$cfg.codex-backup" ]; then
        cp -p "$cfg" "$cfg.codex-backup"
    fi
    chmod 0644 "$cfg.pressuremonitor-new"
    mv -f "$cfg.pressuremonitor-new" "$cfg"
done

sync
