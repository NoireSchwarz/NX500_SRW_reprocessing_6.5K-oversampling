#!/bin/sh
# NX51_CAMERA_RECOVERY_R7: explicit OEM liveview restoration only; no RAW/JPEG task.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
OUT=/mnt/mmc/NXDEMO51
RUN=/tmp/nx-lut-lite
OPS=$D/bin/nxl-fileops
RESTORE=$D/bin/nxl-oem-restore.arm
MENU=$D/bin/nxl-menu
MENU_SHA=60f6cbfe3105c4faca676d846f85ecf9b52c83a3d242fa0ff43dae1ea8e4915b
menu_ok=0 popup=
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
RESTORE_SHA=7dc63f61e40790ab643e0dc128ba140ccb04864cb02c72cced88407b452fe6c6
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
hash(){ "$OPS" sha256 "$1"; }
is_hash(){ [ "${#1}" = 64 ] && case "$1" in *[!0-9a-f]*) return 1;;*) return 0;;esac; }
nxl_progress(){ :; }
if regular "$D/progress.sh";then . "$D/progress.sh";fi
[ "$#" = 1 ] && [ "$1" = --restore-camera ] || exit 2
for folder in "$OUT" "$OUT/logs" "$OUT/recovery";do
    [ ! -L "$folder" ] && { [ ! -e "$folder" ] || [ -d "$folder" ]; } || exit 1
    [ -d "$folder" ] || mkdir "$folder" || exit 1
done
LOG=$OUT/logs/restore-camera.log
[ ! -L "$LOG" ] && { absent "$LOG" || regular "$LOG"; } || exit 1
exec >> "$LOG" 2>&1
printf 'NXOEMRESTORE_WRAPPER revision=recovery-r7 mode=manual pid=%s\n' "$$"
nxl_progress CAMERA_RECOVERY REQUESTED EXPLICIT_OEM_RESTORE_ONLY
notice(){
    [ "$menu_ok" = 1 ] || return 0
    if [ -n "$popup" ];then kill -TERM "$popup" 2>/dev/null || :;wait "$popup" 2>/dev/null || :;popup=;fi
    "$MENU" "$1" </dev/null >> "$LOG" 2>&1 & popup=$!
}
reject(){ notice --demo-camera-restore-failed;printf 'RESTORE_UNCONFIRMED reason=%s\n' "$1";nxl_progress CAMERA_RECOVERY FAILED "RESTORE_UNCONFIRMED; reason=$1";exit 1; }
regular "$OPS" && [ -x "$OPS" ] && [ "$(hash "$OPS")" = "$OPS_SHA" ] || reject FILE_UTILITY_IDENTITY_FAILED
regular "$RESTORE" && [ -x "$RESTORE" ] && [ "$(hash "$RESTORE")" = "$RESTORE_SHA" ] || reject RESTORE_HELPER_IDENTITY_FAILED
regular "$MENU" && [ -x "$MENU" ] && [ "$(hash "$MENU")" = "$MENU_SHA" ] || reject MENU_IDENTITY_FAILED
menu_ok=1
regular "$D/process.sh" || reject PROCESS_HELPER_MISSING
. "$D/process.sh"
nxl_require_normal_mode || reject FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
absent "$D/state/disabled" || reject MODULE_DISABLED
absent "$RUN/session/video_enabled" && absent "$RUN/session/video_pending" || reject VIDEO_LUT_ACTIVE
stamp=0
if command -v date >/dev/null 2>&1;then stamp=$(date +%s 2>/dev/null) || stamp=0;fi
case "$stamp" in ''|*[!0-9]*) stamp=0;;esac
restore_dir=$OUT/recovery/RESTORE_${stamp}_$$
absent "$restore_dir" || reject RESTORE_REPORT_ALREADY_EXISTS
restore_receipt(){
    regular "$1/restore-complete.txt" && regular "$1/result.json" || return 1
    r1= r2= r3= r4= r5= r6= r7= r8= r9= r10= r11= r12= r13= r14= r15= r16= r17= rx=
    {
        IFS= read -r r1 && IFS= read -r r2 && IFS= read -r r3 && IFS= read -r r4 && IFS= read -r r5 &&
        IFS= read -r r6 && IFS= read -r r7 && IFS= read -r r8 && IFS= read -r r9 && IFS= read -r r10 &&
        IFS= read -r r11 && IFS= read -r r12 && IFS= read -r r13 && IFS= read -r r14 && IFS= read -r r15 &&
        IFS= read -r r16 && IFS= read -r r17 && ! IFS= read -r rx && [ -z "$rx" ]
    } < "$1/restore-complete.txt" || return 1
    [ "$r1" = NXOEMRESTORE1 ] && [ "$r2" = classification=RESTORED ] && [ "$r3" = native_calls=1 ] &&
    [ "$r4" = request_submitted=1 ] && [ "$r5" = api_return_verified=1 ] && [ "$r6" = stack_restored=1 ] &&
    [ "$r7" = code_restored=1 ] && [ "$r8" = detached=1 ] && [ "$r9" = api_inflight=0 ] &&
    [ "$r10" = ui_running=1 ] && [ "$r11" = app_expected=0 ] && [ "$r12" = real_state=2 ] &&
    [ "$r13" = request_flag=1 ] && [ "$r14" = fresh_start_completion=1 ] && [ "$r15" = completion_stable=1 ] &&
    [ "$r17" = end=NXOEMRESTOREEND1 ] || return 1
    restore_result_hash=${r16#result_sha256=}
    [ "$r16" = "result_sha256=$restore_result_hash" ] && is_hash "$restore_result_hash" &&
    [ "$(hash "$1/result.json")" = "$restore_result_hash" ]
}

# The helper owns actual run.lock flock and all self-owned engine/trace locks.
# It never adopts an existing hold, frees CAP or commits session state.
notice --demo-camera-restoring
printf 'RESTORE_REPORT=%s\n' "$restore_dir"
"$RESTORE" --restore-camera "$restore_dir"
restore_rc=$?
[ "$restore_rc" = 0 ] && restore_receipt "$restore_dir" || reject RESTORE_UNCONFIRMED
notice --demo-camera-restored
printf 'CAMERA_RESTORED report=%s\n' "$restore_dir"
nxl_progress CAMERA_RECOVERY RESTORED "Original liveview restored; report=$restore_dir"
exit 0
