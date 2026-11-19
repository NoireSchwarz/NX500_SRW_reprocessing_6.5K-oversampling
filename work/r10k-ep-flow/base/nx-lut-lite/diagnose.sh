#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Read-only camera diagnostics: no AF/shutter events and no LUT/settings writes.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
OUT=${1:-/mnt/mmc/NXDEMO51/logs/diagnostic.txt}
# The directory is created only when this explicit diagnostic is requested.
case "$OUT" in /mnt/mmc/NXDEMO51/logs/*)
    [ ! -L /mnt/mmc/NXDEMO51 ] && [ ! -L /mnt/mmc/NXDEMO51/logs ] || exit 1;;esac
case "$OUT" in */*) out_dir=${OUT%/*};;*) out_dir=.;;esac
[ ! -L "$OUT" ] && [ ! -L "$out_dir" ] || exit 1
mkdir -p "$out_dir" || exit 1
export DISPLAY=${DISPLAY:-:0} NX_LUT_ROOT=$D
status_rc=1
status_tmp=$OUT.lut-status.tmp.$$
trap 'rm -f "$status_tmp"' 0
trap 'exit 130' HUP INT TERM
status_snapshot() {
    attempt=1
    while :; do
        status_rc=0
        /bin/sh "$D/lut.sh" status > "$status_tmp" 2>&1 || status_rc=$?
        if [ "$status_rc" -ne 75 ] || [ "$attempt" -ge 5 ]; then break; fi
        printf 'LUT_STATUS_BUSY_RETRY attempt=%s\n' "$attempt"
        sleep 1
        attempt=$((attempt+1))
    done
    cat "$status_tmp"
    printf 'LUT_STATUS_ATTEMPTS=%s LUT_STATUS_EXIT=%s\n' "$attempt" "$status_rc"
}
{
    echo 'NX LUT Lite 0.3.1-controls read-only diagnostic'
    echo '--- boot hook / activation status ---'
    if grep -q '^# NX_LUT_LITE_BT_HOOK_V1$' /usr/sbin/bluetoothd 2>/dev/null; then echo 'Our Bluetooth hook present'; else echo 'Our Bluetooth hook absent'; fi
    [ ! -r "$D/state/startup.status" ] || cat "$D/state/startup.status"
    [ ! -r "$D/state/startup.log" ] || cat "$D/state/startup.log"
    echo '--- version / boot / uptime ---'
    cat /etc/version.info /proc/sys/kernel/random/boot_id /proc/uptime 2>/dev/null
    echo '--- relevant processes (names and IDs only) ---'
    for f in /proc/[0-9]*/status; do
        [ -r "$f" ] || continue
        if { IFS=' 	' read -r label name rest < "$f"; } 2>/dev/null; then
            case "$name" in dfmsd|keyscan|keyscan1|nxl-keys|nxl-menu)
                id=${f%/status}; printf '%s %s\n' "${id##*/}" "$name";;
            esac
        fi
    done
    echo '--- current input bits / X focus (no grabbing or injection) ---'
    "$D/bin/nxl-menu" --inspect 2>&1
    echo '--- LUT status (read-only registry access) ---'
    status_snapshot
    echo '--- pre-apply registry snapshot (read-only file) ---'
    [ ! -r /tmp/nx-lut-lite/session/before.log ] || cat /tmp/nx-lut-lite/session/before.log
    echo '--- screen-colour preference (read only) ---'
    prefman get 0 0x0a704 l 2>&1
    echo '--- previous action log ---'
    [ ! -r /tmp/nx-lut-lite/last-action.log ] || cat /tmp/nx-lut-lite/last-action.log
} > "$OUT" || exit 1
printf 'Saved %s\n' "$OUT"
exit "$status_rc"
