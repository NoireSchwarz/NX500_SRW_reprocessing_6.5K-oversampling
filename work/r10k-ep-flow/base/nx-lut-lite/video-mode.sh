#!/bin/sh
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
MODE=$D/bin/nxl-video-mode.arm
FULL=$D/bin/nxl-video-fullwidth.arm
USB=$D/bin/nxl-usb-guard.arm
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
VIDEO_MODE_SHA=0000000000000000000000000000000000000000000000000000000000000000
VIDEO_FULLWIDTH_SHA=0000000000000000000000000000000000000000000000000000000000000000
USB_GUARD_SHA=0000000000000000000000000000000000000000000000000000000000000000
case "${1:-}" in status|2k|uhd|full|crop80|usb-check) :;;*) exit 2;;esac
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] && [ -f "$MODE" ] && [ ! -L "$MODE" ] && [ -x "$MODE" ] && [ "$("$OPS" sha256 "$MODE")" = "$VIDEO_MODE_SHA" ] || { echo VIDEO_COMPONENT_IDENTITY_FAILED;exit 1; }
[ -f "$FULL" ] && [ ! -L "$FULL" ] && [ -x "$FULL" ] && [ "$("$OPS" sha256 "$FULL")" = "$VIDEO_FULLWIDTH_SHA" ] && [ -f "$USB" ] && [ ! -L "$USB" ] && [ -x "$USB" ] && [ "$("$OPS" sha256 "$USB")" = "$USB_GUARD_SHA" ] || { echo VIDEO_COMPONENT_IDENTITY_FAILED;exit 1; }
usb_check(){
    usb_text=$("$USB" "$1" "$2") && return 0
    usb_reason=USB_CONFIG_GUARD_FAILED
    while IFS= read -r usb_line;do case "$usb_line" in reason=*) usb_reason=${usb_line#reason=};;esac;done <<EOF
$usb_text
EOF
    case "$usb_reason" in ''|*[!A-Z0-9_]*) usb_reason=USB_CONFIG_GUARD_FAILED;;esac
    printf 'NXL_VIDEO_MODE1\nstate=UNKNOWN\nwidth=0\nheight=0\nrecording_or_capture=0\nreason=%s\nend=NXL_VIDEO_MODE_END1\n' "$usb_reason"
    return 1
}
case "$1" in
usb-check) exec "$USB" check STARTUP;;
status) usb_check preflight STATUS || exit 1;exec "$FULL" status;;
esac
usb_check preflight BEFORE_VIDEO || exit 1
# Every native step has an immediate checkpoint, including a failed step.
step(){
    step_phase=$1;shift
    step_rc=0;step_out=$("$@") || step_rc=$?
    usb_check preflight "$step_phase" || return 1
    if [ "$step_rc" != 0 ];then printf '%s\n' "$step_out";return "$step_rc";fi
}
case "$1" in
full)
    step AFTER_UHD_MAPPING "$MODE" uhd || exit 1
    step AFTER_RATE_PATCH "$MODE" 30p || exit 1
    step AFTER_FULLWIDTH "$FULL" apply || exit 1;;
crop80)
    step AFTER_RESTORE "$FULL" restore || exit 1
    step AFTER_OUTPUT_MAPPING "$MODE" uhd || exit 1
    step AFTER_RATE_PATCH "$MODE" 30p || exit 1;;
2k|uhd)
    step AFTER_RESTORE "$FULL" restore || exit 1
    step AFTER_OUTPUT_MAPPING "$MODE" "$1" || exit 1
    if [ "$1" = uhd ];then step AFTER_RATE_RESTORE "$MODE" rate70 || exit 1;fi;;
esac
printf '%s\n' "$step_out"
