#!/bin/sh
set -eu

launcher_dir=/opt/ui/src/ATK-DLRV1126B
target_cfg="$launcher_dir/apk2.cfg"
entry_file=/tmp/rv1126blab-entry.txt

if [ ! -f "$entry_file" ]; then
    exit 10
fi

entry=$(awk 'NF == 3 { print; exit }' "$entry_file")
if [ -z "$entry" ]; then
    exit 20
fi

for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
    if [ ! -f "$cfg" ]; then
        exit 10
    fi
    awk '$3 != "rv1126blab" && NF { print }' "$cfg" > "$cfg.new"
    if [ "$cfg" = "$target_cfg" ]; then
        printf '%s\n' "$entry" >> "$cfg.new"
    fi
    awk 'NF != 3 { bad = 1 } END { exit bad }' "$cfg.new"
done

for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
    if [ ! -f "$cfg.codex-backup" ]; then
        cp -p "$cfg" "$cfg.codex-backup"
    fi
    chmod 0644 "$cfg.new"
    mv -f "$cfg.new" "$cfg"
done

rm -f /tmp/rv1126blab-entry.txt /tmp/rv1126blab-install.sh
sync
