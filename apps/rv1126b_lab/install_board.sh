#!/bin/sh
set -eu

cfg=/opt/ui/src/ATK-DLRV1126B/apk3.cfg
entry_file=/tmp/rv1126blab-entry.txt
new_cfg="$cfg.new"

if [ ! -f "$cfg" ] || [ ! -f "$entry_file" ]; then
    exit 10
fi

if [ ! -f "$cfg.codex-backup" ]; then
    cp -p "$cfg" "$cfg.codex-backup"
fi

awk '
    NR == FNR {
        if (NF == 3)
            entry = $0
        next
    }
    $3 == "rv1126blab" {
        if (!found)
            print entry
        found = 1
        next
    }
    NF { print }
    END {
        if (!entry)
            exit 20
        if (!found)
            print entry
    }
' "$entry_file" "$cfg" > "$new_cfg"

awk 'NF != 3 { bad = 1 } END { exit bad }' "$new_cfg"
chmod 0644 "$new_cfg"
mv -f "$new_cfg" "$cfg"
rm -f /tmp/rv1126blab-entry.txt /tmp/rv1126blab-install.sh
sync
