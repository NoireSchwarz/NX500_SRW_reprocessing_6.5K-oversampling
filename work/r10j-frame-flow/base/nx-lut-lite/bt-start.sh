#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# The Bluetooth wrapper calls this in background. Never apply a LUT here.
set -u
trap '' HUP
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
mkdir -p "$D/state" || exit 1
exec >> "$D/state/startup.log" 2>&1
. "$D/process.sh"
nxl_bt_logdir() {
    (
        sd=$(CDPATH= cd -P "${NXL_PROGRESS_SD:-/mnt/mmc}" 2>/dev/null && pwd -P) || exit 1
        [ -n "$sd" ] && [ "$sd" != / ] || exit 1
        for folder in "$sd/NXDEMO51" "$sd/NXDEMO51/logs";do
            [ ! -L "$folder" ] || exit 1
            if [ -e "$folder" ];then [ -d "$folder" ] || exit 1
            else mkdir "$folder" || exit 1;fi
        done
        printf '%s\n' "$sd/NXDEMO51/logs"
    )
}
publish() {
    target=$(nxl_bt_logdir) || return 0
    for name in startup.log startup.status;do
        [ ! -L "$target/$name" ] && { [ ! -e "$target/$name" ] || [ -f "$target/$name" ]; } || continue
        [ -f "$D/state/$name" ] && [ ! -L "$D/state/$name" ] || continue
        cp "$D/state/$name" "$target/$name" 2>/dev/null || :
    done
    return 0
}
# The installed helper belongs to the module manifest. The fallback also
# publishes only inside NXDEMO51/logs; never recreate legacy card-root logs.
nxl_progress(){ printf 'PROGRESS phase=%s state=%s detail=%s\n' "$1" "$2" "$3";return 0; }

if [ -f "$D/progress.sh" ] && [ ! -L "$D/progress.sh" ];then . "$D/progress.sh";fi
stage() {
    printf '%s\n' "$1";printf '%s\n' "$1" > "$D/state/startup.status"
    case "$1" in
        LISTENER_INPUTS_OPEN*) nxl_progress BLUETOOTH_START SUCCESS "$1";;
        FAILED_*|DISABLED) nxl_progress BLUETOOTH_START ERROR "$1";;
        *) nxl_progress BLUETOOTH_START RUNNING "$1";;
    esac
    publish
}
trap publish 0
stage BT_HOOK_TRIGGERED
[ ! -e "$D/state/disabled" ] || { stage DISABLED; exit 1; }
n=0
while :; do
    [ ! -e "$D/state/disabled" ] || { stage DISABLED; exit 1; }
    if nxl_require_normal_mode &&
       { [ -r /dev/input/event0 ] || [ -r /dev/event0 ]; } &&
       { [ -r /dev/input/event1 ] || [ -r /dev/event1 ]; }; then break; fi
    [ "$n" -lt 30 ] || { stage FAILED_WAITING_NORMAL_MODE_OR_INPUTS; exit 1; }
    n=$((n+1)); sleep 1
done
nxl_require_no_keyscan || { stage FAILED_LEGACY_KEYSCAN_CONFLICT; exit 1; }
# The normal listener starts without a background comparison recorder.
export NXL_INPUT_DEBUG=1
/bin/sh "$D/start.sh" &
child=$!
n=0
while [ "$n" -lt 5 ]; do
    if [ -r /tmp/nx-lut-lite/listener.ready ]; then
        IFS= read -r pid < /tmp/nx-lut-lite/listener.ready || pid=
        case "$pid" in ''|*[!0-9]*) pid=;; esac
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null &&
           [ -r "/proc/$pid/status" ] &&
           { IFS=' 	' read -r label name rest < "/proc/$pid/status"; } &&
           [ "$label" = Name: ] && [ "$name" = nxl-keys ]; then
            stage "LISTENER_INPUTS_OPEN pid=$pid"
            exit 0
        fi
    fi
    if ! kill -0 "$child" 2>/dev/null; then
        wait "$child"; rc=$?
        stage "FAILED_START_EXIT rc=$rc"
        exit 1
    fi
    n=$((n+1)); sleep 1
done
stage FAILED_LISTENER_READINESS_TIMEOUT
exit 1
