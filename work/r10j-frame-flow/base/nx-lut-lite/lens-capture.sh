#!/bin/sh
# Integrated, passive v2 recorder; no workflow lock and no native calls.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WATCH=$D/bin/nxl-lens-watch.arm
OPS=$D/bin/nxl-fileops
LENS_WATCH_SHA=0000000000000000000000000000000000000000000000000000000000000000
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
regular "$OPS" && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] && regular "$WATCH" && [ -x "$WATCH" ] && [ "$("$OPS" sha256 "$WATCH")" = "$LENS_WATCH_SHA" ] || { echo RECORDER_IDENTITY_FAILED;exit 1; }
case "${1:-}" in
start)
    [ "$#" = 1 ] || exit 2
    if report=$("$WATCH" --active 2>/dev/null);then echo "RECORDER_ALREADY_ACTIVE $report";exit 0;fi
    "$WATCH" --usb-ready || exit 1
    mkdir -p /tmp/nx-lut-lite /mnt/mmc/NXDEMO51
    [ ! -L /mnt/mmc/NXDEMO51 ] && [ ! -L /tmp/nx-lut-lite ] || exit 1
    report=/mnt/mmc/NXDEMO51/INIT_LENS_$(date +%s)_$$
    mkdir "$report"
    "$WATCH" "$report" </dev/null > "$report/observer.stdout.txt" 2>&1 &
    watch_pid=$!;tries=0
    while [ "$tries" -lt 15 ];do
        if regular "$report/READY" && actual=$("$WATCH" --active 2>/dev/null) && [ "$actual" = "$report" ];then echo "RECORDER_STARTED $report";exit 0;fi
        if ! kill -0 "$watch_pid" 2>/dev/null;then code=0;wait "$watch_pid" || code=$?;echo "RECORDER_START_FAILED code=$code report=$report";exit 1;fi
        tries=$((tries+1));sleep 1
    done
    [ -f "$report/STOP" ] || : > "$report/STOP"
    echo "RECORDER_READY_TIMEOUT $report";exit 1;;
stop)
    [ "$#" = 1 ] || exit 2
    report=$("$WATCH" --stop) || { echo NO_LIVE_LENS_RECORDER;exit 0; }
    tries=0
    while [ "$tries" -lt 25 ];do
        if regular "$report/DONE";then
            if actual=$("$WATCH" --active 2>/dev/null) && [ "$actual" = "$report" ];then :;else echo "RECORDER_STOPPED $report";cat "$report/DONE";exit 0;fi
        fi
        tries=$((tries+1));sleep 1
    done
    echo "RECORDER_STOP_NOT_CONFIRMED $report";exit 1;;
mark)
    [ "$#" -le 2 ] || exit 2
    mark=${2:-MENU_MARK_$(date +%s)}
    "$WATCH" --mark "$mark";echo "MARKED $mark";;
yield-hardware)
    [ "$#" = 2 ] || exit 2
    "$WATCH" --yield-hardware "$2";;
status)
    [ "$#" = 1 ] || exit 2
    if report=$("$WATCH" --active);then echo "RECORDER_ACTIVE $report";else echo NO_LIVE_LENS_RECORDER;fi;;
*) exit 2;;
esac
