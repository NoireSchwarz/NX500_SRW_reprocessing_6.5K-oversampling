#!/bin/sh
# NX51_INSTALL_ONLY_ENTRY_R5: one deployment, no reboot/boot-ID prerequisite.
task_root=
task_rc=1
SD=$task_root/mnt/mmc
PKG=$SD/nx-lut-demo
STORE=$SD/NXDEMO51
LOGDIR=$STORE/logs
for entry_dir in "$STORE" "$LOGDIR";do
    [ ! -L "$entry_dir" ] || exit 1
    if [ -e "$entry_dir" ];then [ -d "$entry_dir" ] || exit 1
    else mkdir "$entry_dir" || exit 1;fi
done
LOG=$LOGDIR/install.log
[ ! -L "$LOG" ] && { [ ! -e "$LOG" ] || [ -f "$LOG" ]; } || exit 1
NXL_PROGRESS_SD=$SD
NXL_PROGRESS_TO_INSTALL_LOG=1
nxl_progress() {
    (
        LC_ALL=C
        export LC_ALL
        nxlpg_phase=${1:-};nxlpg_state=${2:-};nxlpg_detail=${3:-}
        [ "$#" = 3 ] || exit 0
        [ -n "$nxlpg_phase" ] && [ "${#nxlpg_phase}" -le 48 ] || exit 0
        [ -n "$nxlpg_state" ] && [ "${#nxlpg_state}" -le 48 ] || exit 0
        case "$nxlpg_phase$nxlpg_state" in *[!A-Za-z0-9_-]*) exit 0;;esac
        [ "${#nxlpg_detail}" -le 1024 ] || exit 0
        nxlpg_cr=$(printf '\r')
        case "$nxlpg_detail" in *'
'*|*"$nxlpg_cr"*) exit 0;;esac
        nxlpg_requested=${NXL_PROGRESS_SD:-/mnt/mmc}
        case "$nxlpg_requested" in /*) :;;*) exit 0;;esac
        [ "${#nxlpg_requested}" -le 2048 ] || exit 0
        # NX500 firmware makes /mnt/mmc a Fastlink to /opt/storage/sdcard.
        # Resolve this directory alias once. Status/log LEAVES stay no-link.
        nxlpg_sd=$(CDPATH= cd -P "$nxlpg_requested" 2>/dev/null && pwd -P) || exit 0
        [ -n "$nxlpg_sd" ] && [ "$nxlpg_sd" != / ] && [ -d "$nxlpg_sd" ] || exit 0
        nxlpg_store=$nxlpg_sd/NXDEMO51
        nxlpg_logs=$nxlpg_store/logs
        for nxlpg_dir in "$nxlpg_store" "$nxlpg_logs";do
            [ ! -L "$nxlpg_dir" ] || exit 0
            if [ -e "$nxlpg_dir" ];then [ -d "$nxlpg_dir" ] || exit 0
            else mkdir "$nxlpg_dir" || exit 0;fi
        done
        nxlpg_status=$nxlpg_logs/status.txt
        nxlpg_log=$nxlpg_logs/progress.log
        nxlpg_install_log=$nxlpg_logs/install.log
        nxlpg_regular_or_absent() {
            [ ! -L "$1" ] || return 1
            [ ! -e "$1" ] || [ -f "$1" ]
        }
        nxlpg_regular_or_absent "$nxlpg_status" && nxlpg_regular_or_absent "$nxlpg_log" || exit 0
        nxlpg_time=UNAVAILABLE
        if command -v date >/dev/null 2>&1;then
            nxlpg_observed=$(date -u '+%Y-%m-%dT%H:%M:%SZ' 2>/dev/null) || nxlpg_observed=
            if [ -n "$nxlpg_observed" ] && [ "${#nxlpg_observed}" -le 64 ];then
                case "$nxlpg_observed" in *[!A-Za-z0-9_:TZ+.-]*) :;;*) nxlpg_time=$nxlpg_observed;;esac
            fi
        fi
        nxlpg_note() {
            nxlpg_regular_or_absent "$nxlpg_log" || return 0
            printf 'time=%s phase=%s state=%s %s\n' "$nxlpg_time" "$nxlpg_phase" "$nxlpg_state" "$1" >> "$nxlpg_log" || :
            if [ "${NXL_PROGRESS_TO_INSTALL_LOG:-0}" = 1 ] && nxlpg_regular_or_absent "$nxlpg_install_log";then
                printf 'STATUS_PUBLISH phase=%s state=%s %s\n' "$nxlpg_phase" "$nxlpg_state" "$1" >> "$nxlpg_install_log" || :
            fi
            return 0
        }
        nxlpg_record() {
            printf 'NXDEMO51_PROGRESS1\nphase=%s\nstate=%s\ndetail=%s\ntime=%s\nend=NXDEMO51_PROGRESS_END1\n' "$nxlpg_phase" "$nxlpg_state" "$nxlpg_detail" "$nxlpg_time"
        }
        nxlpg_matches() {
            [ -f "$nxlpg_status" ] && [ ! -L "$nxlpg_status" ] || return 1
            nxlpg_h= nxlpg_p= nxlpg_s= nxlpg_d= nxlpg_t= nxlpg_e= nxlpg_extra=
            { IFS= read -r nxlpg_h && IFS= read -r nxlpg_p && IFS= read -r nxlpg_s && IFS= read -r nxlpg_d && IFS= read -r nxlpg_t && IFS= read -r nxlpg_e && ! IFS= read -r nxlpg_extra; } < "$nxlpg_status" || return 1
            [ -z "$nxlpg_extra" ] && [ "$nxlpg_h" = NXDEMO51_PROGRESS1 ] && [ "$nxlpg_p" = "phase=$nxlpg_phase" ] && [ "$nxlpg_s" = "state=$nxlpg_state" ] && [ "$nxlpg_d" = "detail=$nxlpg_detail" ] && [ "$nxlpg_t" = "time=$nxlpg_time" ] && [ "$nxlpg_e" = end=NXDEMO51_PROGRESS_END1 ]
        }
        nxlpg_sync() {
            if command -v sync >/dev/null 2>&1;then sync || nxlpg_note 'warning=SYNC_FAILED';fi
            return 0
        }
        nxlpg_note "detail=$nxlpg_detail"
        nxlpg_published=0;nxlpg_index=0
        if command -v mv >/dev/null 2>&1;then
            while [ "$nxlpg_index" -lt 16 ];do
                nxlpg_temp=$nxlpg_logs/.status.$$.${nxlpg_index}.tmp
                if [ ! -e "$nxlpg_temp" ] && [ ! -L "$nxlpg_temp" ];then break;fi
                nxlpg_index=$((nxlpg_index+1))
            done
            if [ "$nxlpg_index" -lt 16 ] && (set -C; nxlpg_record > "$nxlpg_temp");then
                if nxlpg_regular_or_absent "$nxlpg_status" && [ ! -L "$nxlpg_temp" ];then
                    nxlpg_mv_rc=0
                    mv -f "$nxlpg_temp" "$nxlpg_status" || nxlpg_mv_rc=$?
                    nxlpg_sync
                    nxlpg_readback=0;nxlpg_matches && nxlpg_readback=1
                    if [ "$nxlpg_mv_rc" = 0 ] && [ "$nxlpg_readback" = 1 ];then nxlpg_published=1
                    else nxlpg_note "warning=STATUS_ATOMIC_PUBLISH_FAILED mv_exit=$nxlpg_mv_rc readback=MISMATCH observed_${nxlpg_p:-phase=UNREADABLE} fallback=INPLACE";fi
                else nxlpg_note 'warning=STATUS_LEAF_CHANGED fallback=REFUSED';exit 0;fi
            else nxlpg_note 'warning=STATUS_TEMP_WRITE_FAILED fallback=INPLACE';fi
        else nxlpg_note 'warning=STATUS_MV_UNAVAILABLE fallback=INPLACE';fi
        if [ "$nxlpg_published" != 1 ];then
            nxlpg_regular_or_absent "$nxlpg_status" || { nxlpg_note 'warning=STATUS_FALLBACK_UNSAFE_LEAF';exit 0; }
            nxlpg_write_rc=0
            # Human status may be rewritten in place when SD replacement is
            # unavailable. Never use it as a task/ownership/completion proof.
            # Shell has no O_NOFOLLOW overwrite primitive: check the leaf
            # immediately before opening, and never accept a symlink leaf.
            if [ -e "$nxlpg_status" ];then
                nxlpg_record > "$nxlpg_status" || nxlpg_write_rc=$?
            else
                (set -C; nxlpg_record > "$nxlpg_status") || nxlpg_write_rc=$?
            fi
            nxlpg_sync
            if [ "$nxlpg_write_rc" = 0 ] && nxlpg_matches;then
                nxlpg_note 'snapshot=INPLACE_VERIFIED'
            else nxlpg_note "warning=STATUS_FALLBACK_FAILED write_exit=$nxlpg_write_rc readback=MISMATCH";fi
        fi
        # Failed temporary records are retained; no old report/hold is deleted.
        exit 0
    ) >/dev/null 2>&1 || :
    return 0
}
nxl_entry_progress() {
    nxl_progress "$@"
    printf 'PROGRESS phase=%s state=%s detail=%s\n' "$1" "$2" "$3" >> "$LOG"
    if command -v sync >/dev/null 2>&1;then sync || :;fi
    return 0
}
# Only diagnostics created by this module are moved. Reserve a unique
# directory atomically so previous archives are never overwritten.
nxl_migrate_old_logs() {
    legacy=$LOGDIR/legacy
    [ ! -L "$legacy" ] || return 0
    if [ -e "$legacy" ];then [ -d "$legacy" ] || return 0
    else mkdir "$legacy" || return 0;fi
    legacy_index=0
    for old_log in "$SD/NX_LUT_DEMO51_INSTALL.log" "$SD/NX_LUT_DEMO51_PROGRESS.log" "$SD/NX_LUT_DEMO51_RUN.log" "$SD/NX_LUT_DEMO51_SESSION.log" "$SD/NX_LUT_DEMO51_LAUNCH.log" "$SD/NX_LUT_DEMO51_STATUS.txt" "$SD/NX_LUT_DEMO49_STARTUP.log" "$SD/NX_LUT_DEMO49_STARTUP.status" "$SD/NX_LUT_RAW422_BOOT.log" "$SD"/NX_LUT_DEMO51_STATUS\ \(*\).txt "$SD"/NX_LUT_DEMO51_STATUS\(*\).txt;do
        [ -f "$old_log" ] && [ ! -L "$old_log" ] || continue
        legacy_name=${old_log##*/}
        case "$legacy_name" in
            'NX_LUT_DEMO51_STATUS ('*')'.txt) legacy_number=${legacy_name#NX_LUT_DEMO51_STATUS (};legacy_number=${legacy_number%).txt};case "$legacy_number" in ''|*[!0-9]*) continue;;esac;;
            'NX_LUT_DEMO51_STATUS('*')'.txt) legacy_number=${legacy_name#NX_LUT_DEMO51_STATUS(};legacy_number=${legacy_number%).txt};case "$legacy_number" in ''|*[!0-9]*) continue;;esac;;
        esac
        archived=0
        while [ "$legacy_index" -lt 256 ];do
            legacy_slot=$legacy/item.$$.${legacy_index};legacy_index=$((legacy_index+1))
            if mkdir "$legacy_slot" 2>/dev/null;then archived=1;break;fi
        done
        [ "$archived" = 1 ] || { printf 'LEGACY_MIGRATION_WARNING no_unique_slot name=%s\n' "$legacy_name" >> "$LOG";return 0; }
        legacy_rc=0
        mv "$old_log" "$legacy_slot/$legacy_name" || legacy_rc=$?
        if [ "$legacy_rc" = 0 ] && [ ! -e "$old_log" ] && [ ! -L "$old_log" ] && [ -f "$legacy_slot/$legacy_name" ] && [ ! -L "$legacy_slot/$legacy_name" ];then
            printf 'LEGACY_DIAGNOSTIC_MOVED name=%s archive=%s\n' "$legacy_name" "$legacy_slot" >> "$LOG"
        else printf 'LEGACY_MIGRATION_WARNING name=%s mv_exit=%s source_retained_or_unverified=1\n' "$legacy_name" "$legacy_rc" >> "$LOG";fi
    done
    return 0
}
# Report entry before invoking any helper or deployment prerequisite.
printf 'NX51_INSTALL_ONLY_ENTRY version=0.4.0-demo51-422 install_revision=recovery-r7\n' >> "$LOG"
nxl_entry_progress INSTALL_ENTRY RUNNING INSTALL_ONLY_NO_AUTOMATIC_REBOOT
nxl_migrate_old_logs
if [ -f "$PKG/deploy.sh" ] && [ ! -L "$PKG/deploy.sh" ];then
    nxl_entry_progress DEPLOY RUNNING VERIFY_AND_INSTALL_MODULE
    /bin/sh "$PKG/deploy.sh" install >> "$LOG" 2>&1
    task_rc=$?
else
    printf 'INSTALL_ERROR: missing or symlinked nx-lut-demo/deploy.sh\n' >> "$LOG"
fi
# Consume only this installer entry. Keep reports and payload for diagnosis.
rm -f "$SD/info.tg" "$SD/nx_cs.adj" "$SD/install.sh"
sync
if [ "$task_rc" = 0 ];then
    nxl_entry_progress INSTALL_COMPLETE SUCCESS POWER_OFF_ON_THEN_BLUETOOTH_MENU_ONLY
else
    # Deployment may have recorded an exact failed phase/reason; preserve it.
    printf 'ENTRY_INSTALL_FAILED exit=%s; see detailed deployment result above\n' "$task_rc" >> "$LOG"
    reported_failure=0
    if [ -f "$LOGDIR/status.txt" ] && [ ! -L "$LOGDIR/status.txt" ];then
        IFS= read -r status_head < "$LOGDIR/status.txt" || status_head=
        if [ "$status_head" = NXDEMO51_PROGRESS1 ];then
            if grep -qx 'state=ERROR' "$LOGDIR/status.txt" || grep -qx 'state=FAILED' "$LOGDIR/status.txt";then reported_failure=1;fi
        fi
    fi
    [ "$reported_failure" = 1 ] || nxl_entry_progress INSTALL_FAILED ERROR SEE_NX_LUT_DEMO51_INSTALL_LOG
fi
# Fixed factory cleanup; never source a rejected SD payload.
for nxlps_file in "$task_root"/proc/[0-9]*/status;do
    [ -r "$nxlps_file" ] || continue
    if { IFS=' 	' read -r nxlps_label nxlps_name nxlps_rest < "$nxlps_file"; } 2>/dev/null;then
        if [ "$nxlps_label" = Name: ] && [ "$nxlps_name" = dfmsd ];then
            nxlps_pid=${nxlps_file%/status};nxlps_pid=${nxlps_pid##*/}
            kill -TERM "$nxlps_pid" 2>/dev/null || :
        fi
    fi
done
exit "$task_rc"
