#!/bin/sh
set -eu

launcher_dir=/opt/ui/src/ATK-DLRV1126B
target_cfg="$launcher_dir/apk2.cfg"
entry_file=/tmp/rv1126blab-entry.txt
adc_entry_file=/tmp/adcled-entry.txt
adc_binary=/opt/ui/src/apps/adcled

if [ ! -f "$entry_file" ] || [ ! -f "$adc_entry_file" ]; then
    exit 10
fi

entry=$(awk 'NF == 3 { print; exit }' "$entry_file")
adc_entry=$(awk 'NF == 3 { print; exit }' "$adc_entry_file")
if [ -z "$entry" ] || [ -z "$adc_entry" ]; then
    exit 20
fi

for cfg in "$launcher_dir/apk1.cfg" "$launcher_dir/apk2.cfg" "$launcher_dir/apk3.cfg"; do
    if [ ! -f "$cfg" ]; then
        exit 10
    fi
    if [ "$cfg" = "$launcher_dir/apk1.cfg" ]; then
        awk '$3 != "rv1126blab" && $3 != "adcled" && NF { print }' "$cfg" > "$cfg.new"
        if [ -x "$adc_binary" ]; then
            printf '%s\n' "$adc_entry" >> "$cfg.new"
        fi
    elif [ "$cfg" = "$target_cfg" ]; then
        printf '%s\n' "$entry" > "$cfg.new"
        awk '$3 != "rv1126blab" && $3 != "adcled" && NF { print }' "$cfg" >> "$cfg.new"
    else
        awk '$3 != "rv1126blab" && $3 != "adcled" && NF { print }' "$cfg" > "$cfg.new"
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

rm -f /tmp/rv1126blab-entry.txt /tmp/adcled-entry.txt /tmp/rv1126blab-install.sh
sync
