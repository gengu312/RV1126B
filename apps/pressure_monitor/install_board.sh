#!/bin/sh
set -eu

launcher_dir=/opt/ui/src/ATK-DLRV1126B
apps_dir=/opt/ui/src/apps
staged_binary=/tmp/pressuremonitor.new
entry_file=/tmp/pressuremonitor-entry.txt
default_config=/tmp/pressuremonitor-default.ini
remote_binary="$apps_dir/pressuremonitor"
data_dir=/userdata/pressure_monitor
config_file="$data_dir/config.ini"
config_staged="$data_dir/.config.ini.new"

cleanup()
{
    rm -f "$staged_binary" "$entry_file" "$default_config" \
        /tmp/pressuremonitor-install.sh
    rm -f "$remote_binary.new"
    rm -f "$config_staged"
    for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
        rm -f "$cfg.pressuremonitor-new"
    done
}
trap cleanup EXIT HUP INT TERM

if [ ! -f "$staged_binary" ] || [ ! -f "$entry_file" ] \
    || [ ! -f "$default_config" ]; then
    exit 10
fi

entry=$(awk 'NF == 3 && $3 == "pressuremonitor" { print; exit }' "$entry_file")
if [ -z "$entry" ]; then
    exit 20
fi

mkdir -p "$apps_dir" "$data_dir/audio"
if [ ! -f "$config_file" ]; then
    install -m 0644 "$default_config" "$config_staged"
    mv -f "$config_staged" "$config_file"
fi
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
