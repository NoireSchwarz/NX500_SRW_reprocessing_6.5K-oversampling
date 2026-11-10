#!/bin/sh
# Explicit passive video trace. The sampler holds no SD output file open.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
PROBE=$D/bin/nxl-video-probe.arm
FLOW=$D/bin/nxl-video-fullwidth.arm
VIDEO_FULLWIDTH_SHA=0000000000000000000000000000000000000000000000000000000000000000
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
VIDEO_PROBE_SHA=0000000000000000000000000000000000000000000000000000000000000000
fail(){ printf 'NXL_VIDEO_PROBE1\nstate=OFF\nreason=%s\nreport=\nsamples=0\nperiod_ms=200\nend=NXL_VIDEO_PROBE_END1\n' "$1";exit 1; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
[ "$#" = 1 ] || exit 2
case "$1" in start|stop|mark|status) :;;*) exit 2;;esac
regular "$OPS" && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] && regular "$PROBE" && [ -x "$PROBE" ] && [ "$("$OPS" sha256 "$PROBE")" = "$VIDEO_PROBE_SHA" ] || fail TRACE_COMPONENT_IDENTITY_FAILED
case "$1" in
status|mark) exec "$PROBE" "--$1";;
start)
    mkdir -p /tmp/nx-lut-lite
    [ ! -L /tmp/nx-lut-lite ] || fail TRACE_PRIVATE_DIRECTORY_INVALID
    current=$("$PROBE" --status) || { printf '%s\n' "$current";exit 1; }
    state=OFF
    while IFS= read -r line;do case "$line" in state=*) state=${line#state=};;esac;done <<EOF
$current
EOF
    case "$state" in ON|SAVING|FULL) printf '%s\n' "$current";exit 0;;PENDING) printf '%s\n' "$current";exit 1;;esac
    ready=$("$PROBE" --ready) || { printf '%s\n' "$ready";exit 1; }
    regular "$FLOW" && [ -x "$FLOW" ] && [ "$("$OPS" sha256 "$FLOW")" = "$VIDEO_FULLWIDTH_SHA" ] || fail TRACE_FLOW_COMPONENT_IDENTITY_FAILED
    # Idle bounded transaction installs observation only; selected output stays intact.
    "$FLOW" trace >/tmp/nx-lut-lite/video-flow-arm.txt 2>&1 || fail TRACE_FLOW_ARM_FAILED_RETURN_PHOTO
    mkdir -p /mnt/mmc/NXDEMO51
    [ ! -L /mnt/mmc/NXDEMO51 ] || fail TRACE_REPORT_DIRECTORY_INVALID
    report=/mnt/mmc/NXDEMO51/VIDEO_STAGE_$(date +%s)_$$
    mkdir "$report"
    log=/tmp/nx-lut-lite/video-start-$$.txt
    "$PROBE" --run "$report" </dev/null >"$log" 2>&1 &
    pid=$!;tries=0
    while [ "$tries" -lt 15 ];do
        current=$("$PROBE" --status) || current=
        active=0;actual=
        while IFS= read -r line;do case "$line" in state=ON) active=1;;report=*) actual=${line#report=};;esac;done <<EOF
$current
EOF
        if [ "$active" = 1 ] && [ "$actual" = "$report" ];then rm -f "$log";printf '%s\n' "$current";exit 0;fi
        if ! kill -0 "$pid" 2>/dev/null;then
            reason=TRACE_START_FAILED
            while IFS= read -r line;do case "$line" in TRACE_*[!A-Z0-9_]*) :;;TRACE_*) reason=$line;;esac;done <"$log"
            rm -f "$log";fail "$reason"
        fi
        tries=$((tries+1));sleep 1
    done
    "$PROBE" --stop >/dev/null 2>&1 || :
    fail TRACE_READY_TIMEOUT;;
stop)
    current=$("$PROBE" --stop) || { printf '%s\n' "$current";exit 1; }
    tries=0
    while [ "$tries" -lt 5 ];do
        case "$current" in *'state=OFF'*) printf '%s\n' "$current";exit 0;;*'state=PENDING'*) printf '%s\n' "$current";exit 1;;esac
        sleep 1;tries=$((tries+1))
        current=$("$PROBE" --status) || { printf '%s\n' "$current";exit 1; }
    done
    printf '%s\n' "$current";;
esac
