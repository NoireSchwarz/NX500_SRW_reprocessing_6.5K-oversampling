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
nxl_progress(){ printf 'STATUS_PUBLISH phase=%s state=%s detail=%s\n' "$1" "$2" "$3" >> "$LOG";return 0; }
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
printf 'NX51_INSTALL_ONLY_ENTRY version=0.4.0-demo51-422 install_revision=recovery-r10k-ep-flow\n' >> "$LOG"
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
    nxl_entry_progress INSTALL_FAILED ERROR SEE_NXDEMO51_LOGS_INSTALL_LOG
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
