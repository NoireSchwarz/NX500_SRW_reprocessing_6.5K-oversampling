#!/bin/sh
# NX52_SHARED208_WORKFLOW1: SRW -> native NV16 LUT -> JPEG; one returned CAP.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
RUN=/tmp/nx-lut-lite
OUT=/mnt/mmc/NXDEMO51
OLD_OUT=/mnt/mmc/NXDEMO50
OEM_SD_ALIAS=/mnt/mmc
OEM_SD_TARGET=/opt/storage/sdcard
PAYLOAD=/mnt/mmc/nx-lut-demo
SESSION=$D/bin/nxl-session.arm
FRONT=$D/bin/nxl-demo-front.arm
POST=$D/bin/nxl-demo-post.arm
HWCTL=$D/bin/nxl-hw-controller.arm
HWWORK=$D/bin/nxl-hw-worker.arm
INIT_PIPELINE=$D/bin/nxl-init-pipeline.arm
OWNED_EP=$D/bin/nxl-owned-ep.arm
INIT_PIPELINE_SHA=0000000000000000000000000000000000000000000000000000000000000000
OWNED_EP_SHA=0000000000000000000000000000000000000000000000000000000000000000
OPS=$D/bin/nxl-fileops
CV=$D/bin/cube2lut.arm
CV_SHA=1fee2e50896dd044025022a2fb3e968a857a7b1c5d1426630d899f5cf286d34d
MENU=$D/bin/nxl-menu
ENGINE=$D/lut.sh
RESTORE=$D/bin/nxl-oem-restore.arm
HANDOFF_WATCH=$D/bin/nxl-handoff-watch.arm
HANDOFF_WATCH_SHA=5fdd4546dee2cdacb2fc77bc27537d3b20c237c862c85a8a5d03f0c5c053c90e
RESTORE_SHA=7dc63f61e40790ab643e0dc128ba140ccb04864cb02c72cced88407b452fe6c6
RESTORE_SCRIPT=$D/restore-camera.sh
RESTORE_SCRIPT_SHA=7e178cce610c7c05324494109713ec8f250846568ec3d8d08a4c8743e7404301
REARM_SCRIPT=$D/rearm-session.sh
REARM_SCRIPT_SHA=e796fceb8813881ef7c7c256040b21ba188079c6d6349ac071731bb2d2afaff8
FRONT_GRANT=$D/state/p49-experiment.state
RETIRE_MANIFEST=$D/data/prior-front-retire.sha256
RETIRE_MANIFEST_SHA=8af1064169dcdc3f1f89bedd84bdd53ace6bf476177f4d338ded8f3b0746d2f4
RETIRE_JOB_NAME=JOB_1793869199_415_10dc3936
RETIRE_REPORT=front_1/front
RETIRE_FALSE_REASON=FRONT_RESTART_OR_PATH_INVALID
PREPARE_PROC=/proc
prepare_full=0
SESSION_SHA=f7187bedb1b9dc2f89b997a70bc9e5aa6618d5414d5de6445509afb0fda3c8db
FRONT_SHA=beda78cd9ac1da78d8b5eb4dc7956c007bea5ce5264d101e950905cf5463f5bb
POST_SHA=aa5a948dd882e910198688de0b4726a5e81cd4b028da8b53a5d6f8c24c529a99
HWCTL_SHA=6e9d80efd4d7ca3d9eaea2011211b49a5bdda4a7c71dc1367e74964977f14a88
HWWORK_SHA=24282f2fe0c6b1453cd06a4abb1250a2be625acf748104df986256155fab9310
MENU_SHA=60f6cbfe3105c4faca676d846f85ecf9b52c83a3d242fa0ff43dae1ea8e4915b
ENGINE_SHA=66095aeab680e897246ac656a3d73da4c80a568459b7faa0cd194077fcba6d15
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
OLD_FRONT_SHA=07f64d2ab1a3bc073a49a2b45fffa4ec4ae41fc5ad7dfddfb6384563be9c7a14
SOURCE_SHA=dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c
PLAN_SHA=d861a9b189e5c17be802a4f72ec5693578d6dc890f7b8bebe7585b577ad1233e
hash(){ "$OPS" sha256 "$1"; }
is_hash(){ [ "${#1}" = 64 ] && case "$1" in *[!0-9a-f]*) return 1;;*) return 0;;esac; }
identifier(){ [ -n "$1" ] && [ "${#1}" -le 80 ] && case "$1" in *[!A-Za-z0-9_-]*) return 1;;*) return 0;;esac; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
is_nonce(){
    case "$1" in ''|0*|*[!0-9]*|???????????*) return 1;;esac
    [ "${#1}" != 10 ] && return 0
    leading=${1%?????????};tail=${1#?}
    case "$leading" in 1|2|3) return 0;;4) [ "$tail" -le 294967295 ];;*) return 1;;esac
}
# A bootstrap failure must be recorded outside the not-yet-created job root.
BOOT_LOG=$OUT/logs/run.log
bootstrap_log(){
    printf 'RAW_BOOTSTRAP phase=%s detail=%s\n' "$1" "$2" >&2
    if [ ! -L "$BOOT_LOG" ] && { [ ! -e "$BOOT_LOG" ] || [ -f "$BOOT_LOG" ]; };then
        printf 'RAW_BOOTSTRAP phase=%s detail=%s\n' "$1" "$2" >> "$BOOT_LOG" || :
    fi
    return 0
}
nxl_progress(){ :; }
case "${1:-}" in --status) :;;*)
    bootstrap_log SCRIPT_ENTERED "revision=recovery-r9w-scaled-lifecycle; script=$0; installed_session=$SESSION; report_root=$OUT"
    sd_physical=$(CDPATH= cd -P -- "$PAYLOAD/.." 2>/dev/null && pwd -P) || sd_physical=UNAVAILABLE
    bootstrap_log SD_PATH "logical=$PAYLOAD/..; physical=$sd_physical"
;;esac
[ -f "$D/process.sh" ] && [ ! -L "$D/process.sh" ] || { bootstrap_log PROCESS_HELPER_FAILED "$D/process.sh";exit 1; }
. "$D/process.sh"
if [ -f "$D/progress.sh" ] && [ ! -L "$D/progress.sh" ];then . "$D/progress.sh";fi
# The C supervisor holds the process-owned workflow flock through child exit.
# Component identities are checked before executing the supervisor itself.
regular "$OPS" && [ -x "$OPS" ] && [ "$(hash "$OPS")" = "$OPS_SHA" ] || { bootstrap_log FILE_UTILITY_IDENTITY_FAILED "$OPS";nxl_progress RAW_COMPONENT_CHECK FAILED FILE_UTILITY_IDENTITY_FAILED;exit 1; }
case "${1:-}" in --status|--new) :;;*)
 absent "$RUN/auto-init-held" || { bootstrap_log AUTO_INIT_UNCONFIRMED "$RUN/auto-init-held";nxl_progress RAW_RESOURCE_CHECK HELD AUTO_INIT_RETURN_UNCONFIRMED;exit 1; }
;;esac
case "${1:-}" in
--rearm)
    regular "$REARM_SCRIPT" && [ "$(hash "$REARM_SCRIPT")" = "$REARM_SCRIPT_SHA" ] || { bootstrap_log REARM_WRAPPER_IDENTITY_FAILED "$REARM_SCRIPT";nxl_progress RAW_RESET_CHECK FAILED REARM_WRAPPER_IDENTITY_FAILED;exit 1; }
    exec /bin/sh "$REARM_SCRIPT" --rearm;;
--restore-camera)
    regular "$RESTORE_SCRIPT" && [ "$(hash "$RESTORE_SCRIPT")" = "$RESTORE_SCRIPT_SHA" ] || { bootstrap_log RESTORE_WRAPPER_IDENTITY_FAILED "$RESTORE_SCRIPT";nxl_progress CAMERA_RECOVERY FAILED RESTORE_WRAPPER_IDENTITY_FAILED;exit 1; }
    exec /bin/sh "$RESTORE_SCRIPT" --restore-camera;;
esac
regular "$SESSION" && [ -x "$SESSION" ] && [ "$(hash "$SESSION")" = "$SESSION_SHA" ] || { bootstrap_log SESSION_MANAGER_IDENTITY_FAILED "$SESSION";nxl_progress RAW_COMPONENT_CHECK FAILED SESSION_MANAGER_IDENTITY_FAILED;exit 1; }
init_trial=0
case "${1:-}" in
--init-trial) shift;exec "$SESSION" "$OUT" run "$0" --init-trial-worker "$@" >> "$BOOT_LOG" 2>&1;;
--init-trial-worker) init_trial=1;shift;;
--status) exec "$SESSION" "$OUT" status;;
--new) exec "$SESSION" "$OUT" new;;
--prepare-full) exec "$SESSION" "$OUT" prepare-full "$0" --prepare-full-worker >> "$BOOT_LOG" 2>&1;;
--prepare-full-worker) [ "${NX51_PREPARE_FULL_ONLY:-}" = 1 ] || exit 1;prepare_full=1;shift;;
--worker) shift;;
*) bootstrap_log SESSION_EXEC "session=$SESSION; command=run; reports=$OUT";nxl_progress RAW_REQUEST REQUESTED "Preparing workflow; reports=$OUT";exec "$SESSION" "$OUT" run "$0" --worker "$@" >> "$BOOT_LOG" 2>&1;;
esac
if [ "$init_trial" = 1 ];then
    case "${NX51_INIT_REPORT:-}" in "$OUT"/INIT_AUTO_*) :;;*) exit 77;;esac
    regular "$INIT_PIPELINE" && [ "$(hash "$INIT_PIPELINE")" = "$INIT_PIPELINE_SHA" ] && regular "$OWNED_EP" && [ "$(hash "$OWNED_EP")" = "$OWNED_EP_SHA" ] || exit 77
    FRONT=$INIT_PIPELINE
    FRONT_SHA=$INIT_PIPELINE_SHA
fi
[ "${NX51_SESSION_ROOT:-}" = "$OUT" ] && [ -n "${NX51_SESSION_TOKEN:-}" ] || exit 1
job=${NX51_JOB:-}
case "$job" in "$OUT"/JOB_*) :;;*) exit 1;;esac
[ -d "$job" ] && [ ! -L "$job" ] || exit 1
exec >> "$job/collector.log" 2>&1
LOOK=raw422
STRENGTH=100
camera_recovery_attempted=0 camera_recovery_verified=0
report= producer= jpeg= child= popup= state= resume= boot= current_boot= lock_boot= front_attempt=0 backend_attempt=0 pipeline_lut=
phase=SETUP reason=NOT_STARTED disposition=SAFE_NO_SUBMIT active_kind=none terminal=ERROR
fresh_state_committed=0 session_reason=
engine_falsehold=0 trace_falsehold=0 engine_owner_hash= engine_hold_hash= trace_owner_hash= trace_hold_hash=
cold_restart_required=0 manager_fail=SAFE_NO_SUBMIT post_rc=-1
engine_owned=0 trace_owned=0 menu_ok=0 native_started=0 native_verified=0 front_no_submit=0 front_mode=new
timing_clock_ready=0 timing_phase= timing_start=UNAVAILABLE
status_read(){
    status_file=$job/.status.$$
    "$SESSION" "$OUT" status > "$status_file" || return 1
    state= resume= boot= current_boot= report= front_attempt= backend_attempt= session_reason=
    while IFS= read -r field;do
        case "$field" in
        reason=*) session_reason=${field#reason=};;state=*) state=${field#state=};;resume=*) resume=${field#resume=};;boot=*) boot=${field#boot=};;
        current_boot=*) current_boot=${field#current_boot=};;front=*) report=${field#front=};;front_attempt=*) front_attempt=${field#front_attempt=};;backend_attempt=*) backend_attempt=${field#backend_attempt=};;
        esac
    done < "$status_file"
    rm -f "$status_file"
    identifier "$state" && identifier "$resume" && [ -n "$boot" ]
}
# Human/frozen-text publication is deliberately independent of rename-overwrite
# persistence. Session journals remain the sole authoritative task state.
publish_text(){
    destination=$1;pending_text=$2
    regular "$pending_text" && [ ! -L "$destination" ] && { absent "$destination" || regular "$destination"; } || return 1
    # Default cp truncates an existing regular inode. Keep the source until
    # its exact bytes have been read back and compared by the bundled helper.
    cp "$pending_text" "$destination" && sync && "$OPS" same "$pending_text" "$destination" || return 1
    rm -f "$pending_text"
}
# Timing is diagnostic only. Read the verified C clock helper; never infer
# hardware state or task authority from timing text or /proc parsing.
timing_now(){
    nxlt_ms=UNAVAILABLE
    if [ "$timing_clock_ready" = 1 ];then
        nxlt_raw=$("$POST" --clock 2>/dev/null) || nxlt_raw=
        case "$nxlt_raw" in
        'NXPOSTCLOCK1 monotonic_ms='*)
            nxlt_value=${nxlt_raw#NXPOSTCLOCK1 monotonic_ms=}
            case "$nxlt_value" in ''|*[!0-9]*) :;;*) [ "${#nxlt_value}" -le 20 ] && nxlt_ms=$nxlt_value;;esac;;
        esac
    fi
    return 0
}
timing_begin(){
    timing_now;timing_phase=$1;timing_start=$nxlt_ms
    printf 'NX_R6_PHASE phase=%s event=BEGIN monotonic_ms=%s\n' "$1" "$nxlt_ms"
    return 0
}
timing_end(){
    timing_now
    # Keep the full uint64 clock decimal intact on32-bit camera shells. The
    # PC analyzer computes END-BEGIN without relying on shell integer width.
    printf 'NX_R6_PHASE phase=%s event=END monotonic_ms=%s begin_monotonic_ms=%s result_code=%s\n' "$1" "$nxlt_ms" "$timing_start" "$2"
    timing_phase=;timing_start=UNAVAILABLE
    return 0
}
record(){
    timing_begin HUMAN_STATUS_COMMIT
    temp=$job/.RESULT.$$
    nxl_progress "$phase" "$1" "reason=$reason; job=$job; attempt=$jpeg"
    printf 'NXDEMO51_RESULT1\nstate_authority=SESSION_SUPERVISOR\ntext_publication=CP_SYNC_EXACT_READBACK\nstate=%s\nphase=%s\nreason=%s\njob=%s\nfront_mode=%s\nfront_source_report=%s\nnative_disposition=%s\ncold_restart_required=%s\nfront_attempt=%s\nbackend_attempt=%s\nattempt_report=%s\njpeg_attempt_dir=%s\n' "$1" "$phase" "$reason" "$job" "$front_mode" "$report" "$disposition" "$cold_restart_required" "$front_attempt" "$backend_attempt" "${attempted_report:-$report}" "$jpeg" > "$temp" && publish_text "$job/RESULT.txt" "$temp"
    nxlt_record_rc=$?
    timing_end HUMAN_STATUS_COMMIT "$nxlt_record_rc"
    return "$nxlt_record_rc"
}
notice(){
    [ "${NX51_PROGRESS_UI:-0}" != 1 ] || return 0
    [ "$menu_ok" = 1 ] || return 0
    if [ -n "$popup" ];then kill -TERM "$popup" 2>/dev/null || :;wait "$popup" 2>/dev/null || :;popup=;fi
    "$MENU" "$1" </dev/null >> "$job/collector.log" 2>&1 & popup=$!
}
lock_owned(){
    [ -d "$1" ] && [ ! -L "$1" ] && regular "$1/pid" && regular "$1/NX51_OWNER" || return 1
    owner_pid= owner_extra=
    { IFS= read -r owner_pid && ! IFS= read -r owner_extra && [ -z "$owner_extra" ]; } < "$1/pid" || return 1
    [ "$owner_pid" = "$$" ] || return 1
    marker_job= marker_boot= marker_head= marker_extra=
    { IFS= read -r marker_head && IFS= read -r marker_job && IFS= read -r marker_boot && ! IFS= read -r marker_extra && [ -z "$marker_extra" ]; } < "$1/NX51_OWNER" || return 1
    [ "$marker_head" = NX51LOCK1 ] && [ "$marker_job" = "job=$job" ] && [ "$marker_boot" = "boot=$lock_boot" ] || return 1
    regular "$1/NX51_ROUND" || return 1
    round_head= round_token= round_pid= round_extra=
    { IFS= read -r round_head && IFS= read -r round_token && IFS= read -r round_pid && ! IFS= read -r round_extra && [ -z "$round_extra" ]; } < "$1/NX51_ROUND" || return 1
    [ "$round_head" = NX51ROUND1 ] && [ "$round_token" = "token=$NX51_SESSION_TOKEN" ] && [ "$round_pid" = "pid=$$" ]
}
lock_take(){
    [ "${#NX51_SESSION_TOKEN}" = 32 ] && case "$NX51_SESSION_TOKEN" in *[!0-9a-f]*) false;;*) true;;esac || return 1
    mkdir "$1" 2>/dev/null || return 1
    printf 'NX51LOCK1\njob=%s\nboot=%s\n' "$job" "$lock_boot" > "$1/NX51_OWNER" &&
    printf 'NX51ROUND1\ntoken=%s\npid=%s\n' "$NX51_SESSION_TOKEN" "$$" > "$1/NX51_ROUND" && printf '%s\n' "$$" > "$1/pid"
}
lock_release(){ lock_owned "$1" && rm -f "$1/pid" "$1/NX51_OWNER" "$1/NX51_ROUND" && "$OPS" unlock "$1" && absent "$1"; }
lock_hold(){
    lock_owned "$1" || return 1
    # Keep the owner marker from the start of the run. A killed runner's PID
    # must never make a live/unknown native task eligible for stale-lock reuse.
    printf 'NX51HOLD1\njob=%s\nboot=%s\nreason=%s\n' "$job" "$lock_boot" "$reason" > "$1/RESTART_REQUIRED" && rm -f "$1/pid"
}
cleanup(){
    code=$?;trap - 0 HUP INT TERM
    if [ "$prepare_full" = 1 ];then prepare_full_cleanup "$code";fi
    if [ -n "$child" ];then kill -TERM "$child" 2>/dev/null || :;wait "$child" 2>/dev/null || :;child=;fi
    if [ -n "$pipeline_lut" ];then rm -f "$pipeline_lut" || :;pipeline_lut=;fi
    if [ -n "${source_ticket:-}" ];then rm -f "$source_ticket" || :;source_ticket=;fi
    if [ "$camera_recovery_attempted" = 1 ] && [ "$camera_recovery_verified" != 1 ];then disposition=UNKNOWN_REQUIRE_RESTART;cold_restart_required=1;fi
    if [ "$disposition" = UNKNOWN_REQUIRE_RESTART ];then
        [ "$code" != 0 ] || reason=NATIVE_CLEANUP_NOT_VERIFIED_AT_COMMIT
        code=1;terminal=ERROR
    fi
    clean=1
    if [ "$disposition" = UNKNOWN_REQUIRE_RESTART ];then
        [ "$engine_owned" != 1 ] || lock_hold "$RUN/engine.lock" || clean=0
        [ "$trace_owned" != 1 ] || lock_hold "$RUN/trace.lock" || clean=0
    else
        [ "$engine_owned" != 1 ] || lock_release "$RUN/engine.lock" || clean=0
        [ "$trace_owned" != 1 ] || lock_release "$RUN/trace.lock" || clean=0
    fi
    if [ "$clean" != 1 ];then code=1;reason=WORKFLOW_LOCK_CLEANUP_FAILED;disposition=UNKNOWN_REQUIRE_RESTART;terminal=ERROR;fi
    if [ "$code" = 0 ];then
        case "$terminal" in
        FRONT_READY) "$SESSION" "$OUT" front-ok "$report" || code=1;;
        COMPLETE) "$SESSION" "$OUT" backend-ok || code=1;;
        PIPELINE_COMPLETE) "$SESSION" "$OUT" pipeline-ok "$report" || code=1;;
        *) code=1;reason=NO_TERMINAL_COMMIT;;
        esac
    fi
    if [ "$code" != 0 ];then
        [ "$reason" != NOT_STARTED ] || reason=WORKFLOW_INTERRUPTED
        manager_fail=$disposition
        if [ "$disposition" = SAFE_RELEASED ] && [ "$cold_restart_required" = 1 ];then manager_fail=SAFE_RELEASED_COLD;fi
        if [ "$disposition" = SAFE_RETAINED ];then manager_fail=SAFE_RETAINED_COLD;fi
        "$SESSION" "$OUT" fail "$manager_fail" "$reason" || :
    fi
    if [ "$active_kind" = pipeline ] || [ "$active_kind" = backend ] || { [ "$active_kind" = front ] && [ "$terminal" != FRONT_READY ]; };then stop_handoff_watch;fi
    attempted_report=$report
    status_read || { state=STATE_UNAVAILABLE;code=1; }
    record "$state" || code=1
    sync || code=1
    if [ "$code" = 0 ];then
        case "$terminal" in FRONT_READY) notice --demo-front-saved;;COMPLETE|PIPELINE_COMPLETE) notice --demo-done;;esac
    elif [ "$reason" = RESTORE_UNCONFIRMED ];then notice --demo-camera-restore-failed
    elif [ "$state" = RESTART_REQUIRED ];then notice --demo-restart-required
    else notice --demo-retryable;fi
    exit "$code"
}
fail(){ reason=$1;exit 1; }
trap cleanup 0
trap 'reason=WORKFLOW_INTERRUPTED;exit 130' HUP INT TERM
if [ "$prepare_full" = 0 ];then
status_read || fail SESSION_STATUS_INVALID
printf '\nNXDEMO51_INVOCATION state=%s boot=%s look=%s strength=%s\n' "$state" "$boot" "$LOOK" "$STRENGTH" >> "$job/workflow.log"
record "$state" || fail RESULT_SAVE_FAILED
fi
check_components(){
    for pair in "MENU:$MENU_SHA" "FRONT:$FRONT_SHA" "POST:$POST_SHA" "HWCTL:$HWCTL_SHA" "HWWORK:$HWWORK_SHA" "ENGINE:$ENGINE_SHA" "RESTORE:$RESTORE_SHA";do
        name=${pair%%:*};want=${pair#*:}
        case "$name" in MENU) program=$MENU;;FRONT) program=$FRONT;;POST) program=$POST;;HWCTL) program=$HWCTL;;HWWORK) program=$HWWORK;;ENGINE) program=$ENGINE;;RESTORE) program=$RESTORE;;esac
        actual=UNAVAILABLE
        if regular "$program";then actual=$(hash "$program") || actual=READ_FAILED;fi
        if ! is_hash "$want" || ! regular "$program" || [ ! -x "$program" ] || [ "$actual" != "$want" ];then
            printf 'COMPONENT_IDENTITY_FAILED component=%s path=%s expected=%s actual=%s runtime=INSTALLED_PRIVATE_MODULE native_started=0\n' "$name" "$program" "$want" "$actual"
            fail "${name}_IDENTITY_FAILED"
        fi
    done
    menu_ok=1
    if [ "$prepare_full" = 0 ];then
        version=$("$FRONT" --version) || fail FRONT_VERSION_FAILED
        case "$version" in 'NXL_DEMO_FRONT=0.3.6-demo50;'*) :;;*) fail FRONT_VERSION_MISMATCH;;esac
    fi
    version=$("$POST" --version) || fail POST_VERSION_FAILED
    case "$version" in 'NXL_RAW422_POST=0.4.0-demo51-422;'*FINAL_CHECK_DELEGATION_R6*) :;;*) fail POST_VERSION_MISMATCH;;esac
    timing_clock_ready=1
}
check_environment(){
    lock_boot=${current_boot:-$boot}
    absent "$D/state/disabled" || fail MODULE_DISABLED
    nxl_require_normal_mode || fail FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
    absent "$RUN/session/video_enabled" && absent "$RUN/session/video_pending" || fail VIDEO_LUT_ACTIVE
    absent "$RUN/trace.lock" || fail MOD_TRACE_LOCK_PRESENT
    absent /tmp/nx-isp-full-capture.lock || fail OTHER_ISP_TRACE_LOCK_PRESENT
    absent /mnt/mmc/NXISP_FULL_ARM || fail SD_ISP_TRACE_ARM_PRESENT
    absent "$RUN/engine.lock" || fail LUT_ENGINE_BUSY
    lock_take "$RUN/engine.lock" || fail LUT_ENGINE_LOCK_FAILED
    engine_owned=1
    lock_take "$RUN/trace.lock" || fail TRACE_LOCK_FAILED
    trace_owned=1
}
configure_raw422(){
    if [ -n "${NX51_SOURCE_SRW:-}" ];then
        source_ticket=$RUN/source-ticket-$NX51_SESSION_TOKEN-$$.bin
        NX51_SOURCE_TICKET=$source_ticket;export NX51_SOURCE_TICKET
        SOURCE_SHA=$("$FRONT" --source-ticket "$NX51_SOURCE_SRW" "$source_ticket") || fail SELECTED_SRW_OR_NATIVE_JPEG_INVALID
        is_hash "$SOURCE_SHA" || fail SELECTED_SRW_HASH_INVALID
        NX51_SOURCE_SHA=$SOURCE_SHA;export NX51_SOURCE_SHA
    fi
    if [ -e "$job/config.txt" ] || [ -L "$job/config.txt" ];then
        regular "$job/config.txt" || fail RAW_CONFIG_UNSAFE
        f1= f2= f3= f4= f5= extra= good=0
        {
            IFS= read -r f1 && IFS= read -r f2 && IFS= read -r f3 && IFS= read -r f4 && IFS= read -r f5 &&
            ! IFS= read -r extra && [ -z "$extra" ] && [ "$f1" = NX52RAW422CONFIG1 ] && [ "$f2" = mode=SINGLE_PIPELINE_SHARED192 ] &&
            [ "$f3" = sampling=422 ] && [ "$f4" = "source_srw_sha256=$SOURCE_SHA" ] && [ "$f5" = "plan_sha256=$PLAN_SHA" ] && good=1
        } < "$job/config.txt"
        [ "$good" = 1 ] || fail ACTIVE_JOB_CONFIG_CHANGED
    else
        printf 'NX52RAW422CONFIG1\nmode=SINGLE_PIPELINE_SHARED192\nsampling=422\nsource_srw_sha256=%s\nplan_sha256=%s\n' "$SOURCE_SHA" "$PLAN_SHA" > "$job/.config.$$" && publish_text "$job/config.txt" "$job/.config.$$" || fail RAW_CONFIG_COMMIT_FAILED
        sync || fail RAW_CONFIG_SYNC_FAILED
    fi
    "$SESSION" "$OUT" configure raw422 100 || fail SESSION_CONFIGURE_FAILED
}
valid_front_path(){
    # Only the documented report shapes are accepted; '*' must not hide
    # extra components. Physical access remains anchored at the OEM SD link.
    case "$1" in
    "$OLD_OUT"/DEMO_*/front)
        relative=${1#"$OLD_OUT"/};report_job=${relative%/front}
        identifier "$report_job" && case "$report_job" in DEMO_*) :;;*) return 1;;esac || return 1;;
    "$OUT"/JOB_*/front_*/front)
        relative=${1#"$OUT"/};report_job=${relative%%/*};remaining=${relative#*/};front_slot=${remaining%/front}
        identifier "$report_job" && case "$report_job" in JOB_*) :;;*) return 1;;esac || return 1
        case "$front_slot" in front_*) front_number=${front_slot#front_};;*) return 1;;esac
        case "$front_number" in ''|0*|*[!0-9]*) return 1;;esac;;
    *) return 1;;
    esac
    case "$1" in *[!A-Za-z0-9_/-]*|*..*) return 1;;esac
    parent=$1
    while [ "$parent" != / ];do
        [ -d "$parent" ] || return 1
        if [ -L "$parent" ];then
            [ "$parent" = "$OEM_SD_ALIAS" ] || return 1
            physical=$(CDPATH= cd -P -- "$parent" 2>/dev/null && pwd -P) || return 1
            [ "$physical" = "$OEM_SD_TARGET" ] || return 1
            target_parent=$OEM_SD_TARGET
            while [ "$target_parent" != / ];do
                [ -d "$target_parent" ] && [ ! -L "$target_parent" ] || return 1
                target_parent=${target_parent%/*};[ -n "$target_parent" ] || target_parent=/
            done
        fi
        parent=${parent%/*};[ -n "$parent" ] || parent=/
    done
    [ -d "$1/case_00" ] && [ ! -L "$1/case_00" ] || return 1
    absent "$1/P49_RESTART_REQUIRED" && absent "$1/P48_RESTART_REQUIRED"
}
validate_front(){
producer=$report/case_00/post-input.txt
[ -f "$report/P49_FRONT_COMPLETE" ] && [ ! -L "$report/P49_FRONT_COMPLETE" ] && [ -f "$report/result.json" ] && [ ! -L "$report/result.json" ] && [ -f "$producer" ] && [ ! -L "$producer" ] || fail FRONT_COMMIT_MISSING
good=0;extra=
{
    IFS= read -r c1 && IFS= read -r c2 && IFS= read -r c3 && IFS= read -r c4 && IFS= read -r c5 && IFS= read -r c6 && IFS= read -r c7 &&
    ! IFS= read -r extra && [ -z "$extra" ] && [ "$c1" = NXP49FRONTCOMPLETE1 ] && [ "$c2" = "plan_sha256=$PLAN_SHA" ] &&
    [ "$c5" = frames_complete=1 ] && [ "$c7" = cold_restart_required=1 ] && good=1
} < "$report/P49_FRONT_COMPLETE"
[ "$good" = 1 ] || fail FRONT_COMMIT_INVALID
case "$c6" in status=PASS|status=WARN_WB_RESTORE) :;;*) fail FRONT_STATUS_INVALID;;esac
root_hash=${c3#result_sha256=};producer_hash=${c4#post_input_sha256=}
[ "$c3" = "result_sha256=$root_hash" ] && [ "$c4" = "post_input_sha256=$producer_hash" ] && is_hash "$root_hash" && is_hash "$producer_hash" || fail FRONT_COMMIT_HASH_INVALID
[ "$(hash "$report/result.json")" = "$root_hash" ] && [ "$(hash "$producer")" = "$producer_hash" ] || fail FRONT_COMMIT_CONTENT_CHANGED
good=0;extra=
{
    IFS= read -r p1 && IFS= read -r p2 && IFS= read -r p3 && IFS= read -r p4 && IFS= read -r p5 && IFS= read -r p6 && IFS= read -r p7 && IFS= read -r p8 &&
    IFS= read -r p9 && IFS= read -r p10 && IFS= read -r p11 && IFS= read -r p12 && IFS= read -r p13 && IFS= read -r p14 && IFS= read -r p15 && IFS= read -r p16 &&
    ! IFS= read -r extra && [ -z "$extra" ] && [ "$p1" = NXP49POSTINPUT1 ] && [ "$p2" = version=0.3.6-demo50 ] &&
    [ "$p3" = "source_srw_sha256=$SOURCE_SHA" ] && [ "$p6" = width=6480 ] && [ "$p7" = height=4320 ] && [ "$p8" = stride=6496 ] &&
    [ "$p10" = source_wb_complete=1 ] && [ "$p11" = cc_source_reloaded=0 ] && [ "$p12" = native_frame_complete=1 ] &&
    [ "$p13" = source_origin_x=0 ] && [ "$p14" = source_origin_y=0 ] && good=1
} < "$producer"
[ "$good" = 1 ] || fail PRODUCER_RECORD_INVALID
y_hash=${p4#y_sha256=};uv_hash=${p5#uv_sha256=};nonce=${p9#nonce=};frame_hash=${p15#front_result_sha256=};audit_hash=${p16#param_dma_audit_sha256=}
[ "$p4" = "y_sha256=$y_hash" ] && [ "$p5" = "uv_sha256=$uv_hash" ] && [ "$p9" = "nonce=$nonce" ] && [ "$p15" = "front_result_sha256=$frame_hash" ] && [ "$p16" = "param_dma_audit_sha256=$audit_hash" ] || fail PRODUCER_FIELD_INVALID
is_hash "$y_hash" && is_hash "$uv_hash" && is_hash "$frame_hash" && is_hash "$audit_hash" || fail PRODUCER_DIGEST_INVALID
is_nonce "$nonce" || fail PRODUCER_NONCE_INVALID
[ -f "$report/case_00/result.json" ] && [ ! -L "$report/case_00/result.json" ] && [ "$(hash "$report/case_00/result.json")" = "$frame_hash" ] || fail FRAME_REPORT_CHANGED
[ -f "$report/case_00/p49_param_dma_audit.bin" ] && [ ! -L "$report/case_00/p49_param_dma_audit.bin" ] && [ "$(hash "$report/case_00/p49_param_dma_audit.bin")" = "$audit_hash" ] || fail PARAM_AUDIT_CHANGED

}
post_command(){
    # The independently pinned runner verifies persisted output after POST
    # returns. Front --verify-input retains its standalone default contract.
    case "${1:-}" in --verify-input) :;;*) set -- --final-check runner-independent "$@";;esac
    exec "$POST" "$@" --y "$report/case_00/p39_output.y8" --uv "$report/case_00/p39_output.uv8" --plane-width 6480 --plane-height 4320 --stride 6496 --crop-x 0 --crop-y 0 --width 6480 --height 4320 --yuv native-nv16 --y-sha256 "$y_hash" --uv-sha256 "$uv_hash" --source-srw-sha256 "$SOURCE_SHA" --producer-report "$producer" --producer-report-sha256 "$producer_hash" --output-dir "$jpeg"
}

verify_input(){
    phase=VERIFY_INPUT
    record "$state" || fail RESULT_SAVE_FAILED
    timing_begin FRONT_INPUT_VERIFY
    post_command --verify-input > "$job/input-verify.stdout.txt" 2>> "$job/collector.log" & child=$!
    rc=0;wait "$child" || rc=$?;child=
    timing_end FRONT_INPUT_VERIFY "$rc"
    [ "$rc" = 0 ] || fail FRONT_INPUT_FULL_VERIFICATION_FAILED
}
reset_live_lut(){
    phase=LUT_FACTORY_RESET
    lock_release "$RUN/engine.lock" || fail ENGINE_HANDOFF_RELEASE_FAILED
    engine_owned=0
    /bin/sh "$ENGINE" off > "$job/live-lut-off.log" 2>&1 & child=$!
    rc=0;wait "$child" || rc=$?;child=
    [ "$rc" = 0 ] || fail LIVE_LUT_RESTORE_FAILED
    lock_take "$RUN/engine.lock" || fail LIVE_LUT_CHANGED_DURING_HANDOFF
    engine_owned=1
    for live in ready current strength pipeline expected.bin video_enabled video_pending expected.new base.new pipeline.new current.new strength.new;do absent "$RUN/session/$live" || fail LIVE_LUT_STATE_CHANGED_AFTER_RESTORE;done
}
front_grant_ready(){
    phase=PREPARE_FRONT
    regular "$FRONT_GRANT" || fail FRONT_REINSTALL_REQUIRED
    grant_head= grant_ready= grant_extra= good=0
    {
        IFS= read -r grant_head && IFS= read -r grant_ready && ! IFS= read -r grant_extra && [ -z "$grant_extra" ] &&
        [ "$grant_head" = NXP33INSTALL1 ] && [ "$grant_ready" = READY=0.3.6-demo50 ] && good=1
    } < "$FRONT_GRANT"
    [ "$good" = 1 ] || fail FRONT_REINSTALL_REQUIRED
}
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
start_handoff_watch(){
    if regular "$D/handoff-watch.sh";then
        /bin/sh "$D/handoff-watch.sh" --start || printf 'INIT_OBSERVER_START_UNAVAILABLE; native workflow admission unchanged\n'
        return 0
    fi
    # This observer closes every inherited descriptor at entry. It cannot
    # retain a supervisor or workflow lock after FRONT_READY is committed.
    watch_dir=$job/handoff-watch-front-$front_attempt
    if regular "$HANDOFF_WATCH" && [ -x "$HANDOFF_WATCH" ] && [ "$(hash "$HANDOFF_WATCH")" = "$HANDOFF_WATCH_SHA" ] && mkdir "$watch_dir";then
        "$HANDOFF_WATCH" "$watch_dir" > "$watch_dir/worker.stdout.txt" 2>&1 &
        printf 'HANDOFF_WATCH_STARTED pid=%s directory=%s\n' "$!" "$watch_dir"
    else
        printf 'HANDOFF_WATCH_UNAVAILABLE; native workflow admission unchanged\n'
    fi
}
stop_handoff_watch(){
    case "$front_attempt" in ''|0|*[!0-9]*) return;;esac
    watch_dir=$job/handoff-watch-front-$front_attempt
    [ -d "$watch_dir" ] && [ ! -L "$watch_dir" ] || return 0
    mkdir "$watch_dir/STOP" 2>/dev/null || :
}
# Included by memory-test.sh after authentication and component checks.
# Keep this in the signed parent script at packaging time.
capture_srw_lut(){
    selected_srw_lut=${NX51_SRW_LUT:-off}
    case "$selected_srw_lut" in off) pipeline_lut=;printf 'table=identity_favorite_selection_off\n' > "$report/srw-lut.origin" || fail LUT_SNAPSHOT_FAILED;return;;esac
    identifier "$selected_srw_lut" || fail SRW_LUT_NAME_INVALID
    srw_looks=${NX_LUT_LOOKS:-/mnt/mmc/looks}
    case "$srw_looks" in /*) :;;*) fail SRW_LUT_DIRECTORY_INVALID;;esac
    [ -d "$srw_looks" ] && [ ! -L "$srw_looks" ] || fail SRW_LUT_DIRECTORY_INVALID
    case "$srw_looks/" in *'/../'*|*'/./'*|*'//'*) fail SRW_LUT_DIRECTORY_INVALID;;esac
    pipeline_lut=$RUN/pipeline-lut-$NX51_SESSION_TOKEN.bin
    absent "$pipeline_lut" || fail LUT_SNAPSHOT_ALREADY_EXISTS
    if regular "$srw_looks/$selected_srw_lut.bin";then
        cp "$srw_looks/$selected_srw_lut.bin" "$pipeline_lut" && "$OPS" same "$srw_looks/$selected_srw_lut.bin" "$pipeline_lut" || fail LUT_SNAPSHOT_FAILED
    elif regular "$srw_looks/$selected_srw_lut.cube";then
        regular "$CV" && [ -x "$CV" ] && [ "$(hash "$CV")" = "$CV_SHA" ] || fail LUT_CONVERTER_IDENTITY_FAILED
        "$CV" "$srw_looks/$selected_srw_lut.cube" "$pipeline_lut" >> "$job/collector.log" 2>&1 || fail SRW_LUT_CONVERSION_FAILED
    else fail SRW_LUT_FILE_MISSING;fi
    "$FRONT" --validate-lut "$pipeline_lut" || fail SRW_LUT_TABLE_INVALID
    selected_lut_hash=$(hash "$pipeline_lut") || fail LUT_SNAPSHOT_FAILED
    printf 'table=favorite_selection\nlook=%s\nlut_sha256=%s\nstrength=100\n' "$selected_srw_lut" "$selected_lut_hash" > "$report/srw-lut.origin" || fail LUT_SNAPSHOT_FAILED
}
idle_receipt(){
    regular "$1/idle-complete.txt" && regular "$1/result.json" || return 1
    i1= i2= i3= i4= i5= i6= i7= i8= i9= i10= i11= i12= i13= i14= ix=
    {
        IFS= read -r i1 && IFS= read -r i2 && IFS= read -r i3 && IFS= read -r i4 && IFS= read -r i5 &&
        IFS= read -r i6 && IFS= read -r i7 && IFS= read -r i8 && IFS= read -r i9 && IFS= read -r i10 &&
        IFS= read -r i11 && IFS= read -r i12 && IFS= read -r i13 && IFS= read -r i14 && ! IFS= read -r ix && [ -z "$ix" ]
    } < "$1/idle-complete.txt" || return 1
    [ "$i1" = NXOEMIDLE1 ] && [ "$i2" = classification=IDLE_VERIFIED ] && [ "$i3" = readonly=1 ] &&
    [ "$i4" = native_calls=0 ] && [ "$i5" = request_submitted=0 ] && [ "$i6" = workspace_return_verified=1 ] &&
    [ "$i7" = still_idle=1 ] && [ "$i8" = backend_idle=1 ] && [ "$i9" = callbacks_retired=1 ] &&
    [ "$i10" = stable_samples=3 ] && [ "$i11" = original_code_verified=1 ] && [ "$i12" = no_liveview_start=1 ] &&
    [ "$i14" = end=NXOEMIDLEEND1 ] || return 1
    idle_hash=${i13#result_sha256=}
    [ "$i13" = "result_sha256=$idle_hash" ] && is_hash "$idle_hash" && [ "$(hash "$1/result.json")" = "$idle_hash" ]
}
restore_after_pipeline(){
    nxl_progress PIPE_IDLE_VERIFY RUNNING "Checking returned workspace and native idle state"
    camera_recovery_attempted=1
    phase=NATIVE_IDLE_VERIFY;reason=NATIVE_IDLE_UNCONFIRMED
    record BACKEND_RUNNING || fail RESULT_SAVE_FAILED
    restore_dir=$job/restore-camera-pipeline-$backend_attempt
    absent "$restore_dir" || fail RESTORE_REPORT_ALREADY_EXISTS
    timing_begin OEM_CAMERA_RESTORE
    "$RESTORE" --supervised-idle "$restore_dir" > "$job/restore-front-$front_attempt.stdout.txt" 2>> "$job/collector.log" & child=$!
    restore_rc=0;wait "$child" || restore_rc=$?;child=
    timing_end OEM_CAMERA_RESTORE "$restore_rc"
    [ "$restore_rc" = 0 ] && idle_receipt "$restore_dir" || fail NATIVE_IDLE_UNCONFIRMED
    camera_recovery_verified=1
}
verify_jpeg(){
    nxl_progress PIPE_VERIFY RUNNING "Verifying the saved final JPEG"
    timing_begin FINAL_PERSISTED_VERIFY
    "$FRONT" --verify-output-fast "$jpeg" > "$job/post-verify-$backend_attempt.stdout.txt" 2>> "$job/collector.log" & child=$!
    rc=0;wait "$child" || rc=$?;child=
    timing_end FINAL_PERSISTED_VERIFY "$rc"
    [ "$rc" = 0 ] || fail JPEG_OUTPUT_VERIFICATION_FAILED
}
run_pipeline(){
    front_grant_ready
    "$SESSION" "$OUT" begin pipeline || fail PIPELINE_BEGIN_NOT_COMMITTED
    status_read || fail SESSION_STATUS_INVALID
    case "$front_attempt:$backend_attempt" in *[!0-9:]*|0:*|*:0|:*) fail PIPELINE_ATTEMPT_INVALID;;esac
    front_parent=$job/front_$front_attempt
    mkdir "$front_parent" || fail FRONT_ATTEMPT_DIRECTORY_NOT_NEW
    report=$front_parent/front
    mkdir "$report" || fail FRONT_REPORT_NOT_NEW
    [ -d "$job/jpeg" ] || mkdir "$job/jpeg" || fail JPEG_PARENT_CREATE_FAILED
    [ ! -L "$job/jpeg" ] || fail JPEG_PARENT_UNSAFE
    jpeg=$job/jpeg/attempt_$backend_attempt
    absent "$jpeg" || fail JPEG_ATTEMPT_DIRECTORY_NOT_NEW
    capture_srw_lut
    reset_live_lut
    phase=NATIVE_SRW_LUT_JPEG;active_kind=pipeline;disposition=UNKNOWN_REQUIRE_RESTART
    reason=SINGLE_PIPELINE_RUNNING
    record FRONT_RUNNING || fail RESULT_SAVE_FAILED
    notice --demo-ready
    # No continuous initialization recorder in the compact production path.
    timing_begin NATIVE_SINGLE_PIPELINE
    "$FRONT" --pipeline "$report" "$jpeg" "$pipeline_lut" > "$job/pipeline-$front_attempt.stdout.txt" 2>> "$job/collector.log" & child=$!
    rc=0;wait "$child" || rc=$?;child=
    timing_end NATIVE_SINGLE_PIPELINE "$rc"
    # Dedicated exits are emitted only by the identity-checked single entry.
    # No READY polling, owner wait process, front-ok or backend begin exists.
    case "$rc" in
    0) disposition=SAFE_RELEASED;cold_restart_required=1;native_verified=1;;
    42) disposition=SAFE_NO_SUBMIT;cold_restart_required=0;fail PIPELINE_NEEDS_BOOT_INITIALIZATION;;
    43) disposition=SAFE_NO_SUBMIT;cold_restart_required=0;fail PIPELINE_BANK2_CAPACITY_UNAVAILABLE;;
    44) disposition=SAFE_RELEASED;cold_restart_required=1
        primary=;extra=
        if regular "$jpeg/STATUS.reason" && { IFS= read -r primary && ! IFS= read -r extra && [ -z "$extra" ]; } < "$jpeg/STATUS.reason";then
            case "$primary" in LDC_CONFIGURATION_READBACK_FAILED_WORKSPACE_RETURNED|LDC_FAILED_WORKSPACE_RETURNED|LUT_FAILED_WORKSPACE_RETURNED|JPEG_FAILED_WORKSPACE_RETURNED) fail "$primary";;esac
        fi
        fail PIPELINE_FAILED_WORKSPACE_RETURNED;;
    45) disposition=SAFE_NO_SUBMIT;cold_restart_required=0;fail PIPELINE_SOURCE_SETTINGS_OR_SETUP_FAILED;;
    46) disposition=SAFE_NO_SUBMIT;cold_restart_required=0;fail FRONT_CODE_GUARD_REJECTED_NO_SUBMIT;;
    *) disposition=UNKNOWN_REQUIRE_RESTART;cold_restart_required=1;fail PIPELINE_NATIVE_STATE_UNVERIFIED;;
    esac
    regular "$report/shared-workspace.bin" && regular "$report/result.json" || fail PIPELINE_RECEIPT_MISSING
    phase=VERIFY_JPEG
    verify_jpeg
    restore_after_pipeline
    cold_restart_required=0
    terminal=PIPELINE_COMPLETE;reason=SINGLE_SRW_LUT_JPEG_SHARED192_VERIFIED
    final_image=;extra=
    regular "$jpeg/output-path.txt" && { IFS= read -r final_image && ! IFS= read -r extra && [ -z "$extra" ]; } < "$jpeg/output-path.txt" || fail OUTPUT_PATH_SAVE_FAILED
    case "$final_image" in /mnt/mmc/DCIM/*/NXLI????.JPG) :;;*) fail OUTPUT_PATH_SAVE_FAILED;;esac
    printf 'native=%s\nsampling=422\nLUT=NATIVE_ISP_SHARED192\nentry=ONE_PROCESS_FOUR_STAGES\n' "$final_image" > "$job/output-files.txt" || fail OUTPUT_PATH_SAVE_FAILED
    exit 0
}
# This preparation retires only the exact false-path rejection represented by
# the pinned, independently verified report. Old pixels are read as evidence
# only. The next normal action always decodes SRW and submits a new ISP job.
prepare_processes_idle(){
    [ -r "$PREPARE_PROC/self/status" ] || return 1
    parent_supervisor=${PPID:-0}
    case "$parent_supervisor" in ''|0|*[!0-9]*) return 1;;esac
    for proc_status in "$PREPARE_PROC"/[0-9]*/status;do
        [ -r "$proc_status" ] || continue
        if { IFS=' 	' read -r label proc_name rest < "$proc_status"; } 2>/dev/null;then
            [ "$label" = Name: ] || return 1
            proc_pid=${proc_status%/status};proc_pid=${proc_pid##*/}
            case "$proc_name" in
            nxl-demo-front|nxl-demo-front.|nxl-native-fron|nxl-demo-post|nxl-demo-post.a|nxl-allocprobe.|nxl-hw-controll|nxl-hw-worker.a|nxl-bitbworker.) return 1;;
            nxl-session.arm) [ "$proc_pid" = "$parent_supervisor" ] || return 1;;
            esac
        fi
    done
}
falsehold_check(){
    falsehold_exists=0;falsehold_owner_hash=;falsehold_hold_hash=
    absent "$1" && return 0
    [ -d "$1" ] && [ ! -L "$1" ] && absent "$1/pid" && regular "$1/NX51_OWNER" && regular "$1/RESTART_REQUIRED" || return 1
    for hold_entry in "$1"/* "$1"/.[!.]* "$1"/..?*;do
        [ -e "$hold_entry" ] || [ -L "$hold_entry" ] || continue
        case "${hold_entry##*/}" in NX51_OWNER|RESTART_REQUIRED) :;;*) return 1;;esac
    done
    owner_head= owner_job= owner_boot= extra=
    { IFS= read -r owner_head && IFS= read -r owner_job && IFS= read -r owner_boot && ! IFS= read -r extra && [ -z "$extra" ]; } < "$1/NX51_OWNER" || return 1
    [ "$owner_head" = NX51LOCK1 ] && [ "$owner_job" = "job=$job" ] || return 1
    hold_boot=${owner_boot#boot=};[ "$owner_boot" = "boot=$hold_boot" ] && identifier "$hold_boot" || return 1
    held_head= held_job= held_boot= held_reason= extra=
    { IFS= read -r held_head && IFS= read -r held_job && IFS= read -r held_boot && IFS= read -r held_reason && ! IFS= read -r extra && [ -z "$extra" ]; } < "$1/RESTART_REQUIRED" || return 1
    [ "$held_head" = NX51HOLD1 ] && [ "$held_job" = "job=$job" ] && [ "$held_boot" = "$owner_boot" ] && [ "$held_reason" = "reason=$RETIRE_FALSE_REASON" ] || return 1
    falsehold_owner_hash=$(hash "$1/NX51_OWNER") && falsehold_hold_hash=$(hash "$1/RESTART_REQUIRED") || return 1
    is_hash "$falsehold_owner_hash" && is_hash "$falsehold_hold_hash" || return 1
    falsehold_exists=1
}
falsehold_release(){
    saved_exists=$2;saved_owner=$3;saved_hold=$4
    falsehold_check "$1" || return 1
    [ "$falsehold_exists" = "$saved_exists" ] || return 1
    [ "$saved_exists" = 1 ] || return 0
    [ "$falsehold_owner_hash" = "$saved_owner" ] && [ "$falsehold_hold_hash" = "$saved_hold" ] || return 1
    # Only these two already-verified workflow marker files are removed. No
    # stock resource, CAP allocation, PID, signal or unknown lock is touched.
    rm -f "$1/NX51_OWNER" "$1/RESTART_REQUIRED" && "$OPS" unlock "$1" && absent "$1"
}
prepare_event_evidence(){
    regular "$job/events.log" || return 1
    events_count=0
    while IFS= read -r event_line;do
        events_count=$((events_count+1))
        case "$events_count" in
        1) expected_event='seq=1 event=NEW_SESSION state=NEED_STILL_INIT stage=idle outcome=NONE reason=NOT_STARTED';;
        2) expected_event='seq=2 event=configure state=NEED_STILL_INIT stage=idle outcome=NONE reason=NOT_STARTED';;
        3) expected_event='seq=3 event=begin state=FRONT_RUNNING stage=front outcome=RUNNING reason=TASK_STARTED';;
        4) expected_event="seq=4 event=fail state=RESTART_REQUIRED stage=front outcome=UNKNOWN_REQUIRE_RESTART reason=$RETIRE_FALSE_REASON";;
        *) return 1;;
        esac
        [ "$event_line" = "$expected_event" ] || return 1
    done < "$job/events.log"
    # Text evidence only narrows eligibility. It is never state/completion
    # authority; exact pinned native proof and full input verification follow.
    [ -z "${event_line:-}" ] && [ "$events_count" = 4 ]
}

verify_retire_manifest(){
    regular "$RETIRE_MANIFEST" && is_hash "$RETIRE_MANIFEST_SHA" && [ "$(hash "$RETIRE_MANIFEST")" = "$RETIRE_MANIFEST_SHA" ] || fail RETIRE_MANIFEST_IDENTITY_FAILED
    retire_count=0
    while IFS=' ' read -r wanted relative extra;do
        [ -z "$extra" ] && is_hash "$wanted" || fail RETIRE_MANIFEST_FORMAT_INVALID
        case "$relative" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) fail RETIRE_MANIFEST_PATH_INVALID;;esac
        case "$relative" in */*) [ "${relative%/*}" = case_00 ] || fail RETIRE_MANIFEST_PATH_INVALID;;esac
        regular "$report/$relative" && [ "$(hash "$report/$relative")" = "$wanted" ] || fail RETIRE_FRONT_EVIDENCE_CHANGED
        retire_count=$((retire_count+1));[ "$retire_count" -le 128 ] || fail RETIRE_MANIFEST_TOO_LARGE
    done < "$RETIRE_MANIFEST"
    [ -z "${wanted:-}${relative:-}${extra:-}" ] && [ "$retire_count" -gt 0 ] || fail RETIRE_MANIFEST_TRUNCATED
}
prepare_full_cleanup(){
    code=$1
    if [ -n "$child" ];then kill -TERM "$child" 2>/dev/null || :;wait "$child" 2>/dev/null || :;child=;fi
    if [ "$code" != 0 ] && [ "$fresh_state_committed" = 1 ];then
        "$SESSION" "$OUT" fail UNKNOWN_REQUIRE_RESTART "$reason" || :
    fi
    proof_report=$report
    status_read || { state=STATE_UNAVAILABLE;code=1; }
    attempted_report=$proof_report
    record "$state" || {
        code=1;reason=FULL_PREPARE_RESULT_SAVE_FAILED
        if [ "$fresh_state_committed" = 1 ];then "$SESSION" "$OUT" fail UNKNOWN_REQUIRE_RESTART "$reason" || :;fi
    }
    sync || code=1
    if [ "$code" = 0 ];then notice --demo-full-ready;else notice --demo-restart-required;fi
    exit "$code"
}
prepare_full_work(){
    phase=PREPARE_FULL_PROOFS;reason=FULL_PREPARE_NOT_VERIFIED
    check_components
    status_read || fail SESSION_STATUS_INVALID
    [ "$state" = RESTART_REQUIRED ] || fail FULL_PREPARE_STATE_NOT_HELD
    case "$session_reason" in LEGACY_STATE_DIVERGENCE|FRONT_RESTART_OR_PATH_INVALID) :;;*) fail FULL_PREPARE_REASON_NOT_ELIGIBLE;;esac
    [ "$job" = "$OUT/$RETIRE_JOB_NAME" ] || fail FULL_PREPARE_JOB_NOT_ELIGIBLE
    [ "$backend_attempt" = 0 ] && absent "$job/jpeg" || fail FULL_PREPARE_JPEG_ATTEMPT_PRESENT
    [ "$front_attempt" = 1 ] || fail FULL_PREPARE_FRONT_ATTEMPT_NOT_ELIGIBLE
    absent "$D/state/disabled" || fail MODULE_DISABLED
    nxl_require_normal_mode || fail FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
    absent "$RUN/session/video_enabled" && absent "$RUN/session/video_pending" || fail VIDEO_LUT_ACTIVE
    for unknown in "$RUN"/*.lock "$RUN"/.*.lock;do
        absent "$unknown" && continue
        case "$unknown" in
        "$RUN/engine.lock"|"$RUN/trace.lock") :;;
        "$RUN/menu.lock"|"$RUN/memory-ui.lock"|"$RUN/keys.lock"|"$RUN/trace-ui.lock")
            regular "$unknown" || fail FULL_PREPARE_OTHER_HOLD_PRESENT;;
        *) fail FULL_PREPARE_OTHER_HOLD_PRESENT;;
        esac
    done
    absent "$RUN/trace-worker.pid" && absent /tmp/nx-isp-full-capture.lock && absent /mnt/mmc/NXISP_FULL_ARM || fail FULL_PREPARE_OTHER_HOLD_PRESENT
    for front_entry in "$job"/front_*;do
        absent "$front_entry" && continue
        [ "$front_entry" = "$job/front_1" ] && [ -d "$front_entry" ] && [ ! -L "$front_entry" ] || fail FULL_PREPARE_EXTRA_FRONT_PRESENT
    done
    prepare_processes_idle || fail FULL_PREPARE_NATIVE_WORKER_PRESENT
    falsehold_check "$RUN/engine.lock" || fail FULL_PREPARE_ENGINE_HOLD_MISMATCH
    engine_falsehold=$falsehold_exists;engine_owner_hash=$falsehold_owner_hash;engine_hold_hash=$falsehold_hold_hash
    falsehold_check "$RUN/trace.lock" || fail FULL_PREPARE_TRACE_HOLD_MISMATCH
    trace_falsehold=$falsehold_exists;trace_owner_hash=$falsehold_owner_hash;trace_hold_hash=$falsehold_hold_hash
    prepare_event_evidence || fail FULL_PREPARE_EVENT_EVIDENCE_INVALID
    report=$job/$RETIRE_REPORT
    [ "$RETIRE_REPORT" = front_1/front ] && valid_front_path "$report" || fail FULL_PREPARE_FRONT_PATH_INVALID
    # The manifest pins the exact previously verified native completion and
    # cleanup bytes, including source/audits/guards. It is not a new input.
    verify_retire_manifest
    validate_front
    jpeg=$job/unused-full-preparation-verify
    verify_input
    # Recheck current owners after slow hashing, before changing control data.
    prepare_processes_idle || fail FULL_PREPARE_NATIVE_WORKER_PRESENT
    falsehold_check "$RUN/engine.lock" || fail FULL_PREPARE_ENGINE_HOLD_MISMATCH
    [ "$falsehold_exists:$falsehold_owner_hash:$falsehold_hold_hash" = "$engine_falsehold:$engine_owner_hash:$engine_hold_hash" ] || fail FULL_PREPARE_ENGINE_HOLD_CHANGED
    falsehold_check "$RUN/trace.lock" || fail FULL_PREPARE_TRACE_HOLD_MISMATCH
    [ "$falsehold_exists:$falsehold_owner_hash:$falsehold_hold_hash" = "$trace_falsehold:$trace_owner_hash:$trace_hold_hash" ] || fail FULL_PREPARE_TRACE_HOLD_CHANGED
    phase=PREPARE_FULL_COMMIT;reason=FULL_PREPARE_COMMIT_FAILED
    "$SESSION" "$OUT" fresh-front "$report" || fail FULL_PREPARE_COMMIT_FAILED
    fresh_state_committed=1
    grant_temp=$D/state/.p49-experiment.$$
    printf 'NXP33INSTALL1\nREADY=0.3.6-demo50\n' > "$grant_temp" && publish_text "$FRONT_GRANT" "$grant_temp" || fail FULL_PREPARE_GRANT_COMMIT_FAILED
    phase=PREPARE_FULL_RELEASE
    falsehold_release "$RUN/engine.lock" "$engine_falsehold" "$engine_owner_hash" "$engine_hold_hash" || fail FULL_PREPARE_ENGINE_RELEASE_FAILED
    falsehold_release "$RUN/trace.lock" "$trace_falsehold" "$trace_owner_hash" "$trace_hold_hash" || fail FULL_PREPARE_TRACE_RELEASE_FAILED
    reason=FULL_TEST_PREPARED_NO_REUSE;disposition=SAFE_NO_SUBMIT;terminal=FULL_TEST_READY
}

if [ "$prepare_full" = 1 ];then prepare_full_work;exit 0;fi
check_components
check_environment
configure_raw422
case "$state" in
NEED_STILL_INIT) run_pipeline;;
NEED_JPEG_INIT) fail OLD_SPLIT_JOB_START_NEW_PIPELINE;;
RETRYABLE)
    case "$resume" in front) run_pipeline;;backend) fail OLD_SPLIT_JOB_START_NEW_PIPELINE;;*) fail SESSION_RETRY_STAGE_INVALID;;esac;;
RESTART_REQUIRED) disposition=UNKNOWN_REQUIRE_RESTART;fail RESOURCE_CHECK_REQUIRED;;
*) fail SESSION_ACTION_NOT_ALLOWED;;
esac
