#!/bin/sh
# Isolated evidence recorder. No main package installation or workflow changes.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
WATCH=$D/bin/nxl-init-watch.arm
AUTO_WATCH_SHA=0000000000000000000000000000000000000000000000000000000000000000
[ -x "$OPS" ] && [ -x "$WATCH" ] && [ "$("$OPS" sha256 "$WATCH")" = "$AUTO_WATCH_SHA" ] || { echo RECORDER_IDENTITY_FAILED;exit 1; }
fw_version= fw_model=
{ IFS= read -r fw_version && IFS= read -r fw_model; } < /etc/version.info || exit 1
[ "$(printf '%s' "$fw_version" | tr -d '\r')" = 1.12 ] && [ "$(printf '%s' "$fw_model" | tr -d '\r')" = NX500 ] || { echo NX500_1_12_REQUIRED;exit 1; }
report_path(){
    [ -f "$D/state/auto-init.active" ] && [ ! -L "$D/state/auto-init.active" ] || return 1
    report=$(cat "$D/state/auto-init.active")
    case "$report" in /mnt/mmc/NXDEMO51/INIT_AUTO_*|/mnt/mmc/NXDEMO51/INIT_LENS_*) :;;*) return 1;;esac
    case "$report" in *..*|*[!A-Za-z0-9_/-]*) return 1;;esac
    [ -d "$report" ] && [ ! -L "$report" ]
}
case "${1:-}" in
start)
    profile=${2:-LENS};case "$profile" in AUTO|LENS) :;;*) exit 2;;esac
    # The recorder's process-owned flock is the authority. A previous report
    # without DONE (power loss or reboot) never becomes a permanent busy flag.
    mkdir -p /tmp/nx-lut-lite /mnt/mmc/NXDEMO51
    report=/mnt/mmc/NXDEMO51/INIT_${profile}_$(date +%s)_$$
    mkdir "$report"
    cat /proc/sys/kernel/random/boot_id > "$report/boot-id.txt"
    "$WATCH" "$report" </dev/null > "$report/observer.stdout.txt" 2>&1 &
    watch_pid=$!;tries=0
    while [ "$tries" -lt 10 ];do
        if [ -f "$report/READY" ];then
            printf '%s\n' "$report" > "$D/state/auto-init.active.$$"
            mv "$D/state/auto-init.active.$$" "$D/state/auto-init.active"
            echo "RECORDER_STARTED $report";exit 0
        fi
        kill -0 "$watch_pid" 2>/dev/null || { echo OBSERVER_BUSY_OR_UNAVAILABLE;exit 1; }
        tries=$((tries+1));sleep 1
    done
    mkdir "$report/STOP" 2>/dev/null || :
    echo RECORDER_READY_TIMEOUT;exit 1;;
stop)
    report_path || { echo NO_OWN_RECORDER;exit 1; }
    if [ "$(cat "$report/boot-id.txt")" != "$(cat /proc/sys/kernel/random/boot_id)" ];then echo PREVIOUS_BOOT_NO_LIVE_RECORDER;exit 0;fi
    [ -f "$report/DONE" ] || mkdir "$report/STOP" 2>/dev/null || :
    tries=0
    while [ "$tries" -lt 20 ];do
        if [ -f "$report/DONE" ];then echo "RECORDER_STOPPED $report";exit 0;fi
        tries=$((tries+1));sleep 1
    done
    echo "RECORDER_STOP_NOT_CONFIRMED $report";exit 1;;
mark)
    report_path;mark=${2:-}
    case "$mark" in ''|*[!A-Za-z0-9_-]*) exit 2;;esac
    [ ! -f "$report/DONE" ] || { echo RECORDER_ALREADY_DONE;exit 1; }
    printf '%s\t%s\t%s\n' "$(date +%s)" "$(cat /proc/uptime)" "$mark" >> "$report/marks.tsv"
    echo "MARKED $mark";;
status)
    report_path
    if [ "$(cat "$report/boot-id.txt")" != "$(cat /proc/sys/kernel/random/boot_id)" ];then echo PREVIOUS_BOOT_REPORT;exit 0;fi
    if [ -f "$report/DONE" ];then cat "$report/DONE";else cat "$report/READY";fi;;
*) echo 'capture.sh start [LENS|AUTO] | mark NAME | status | stop';exit 2;;
esac
