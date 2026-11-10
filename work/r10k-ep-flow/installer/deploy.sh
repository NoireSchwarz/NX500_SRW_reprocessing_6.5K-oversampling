#!/bin/sh
# NXDEMO51_FULL_DEPLOY_V1 - installed only by the SD factory entry.
set -eu
task_root=
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
ACTION=${1:-install}
SD=$task_root/mnt/mmc
PKG=$SD/nx-lut-demo
SRC=$PKG/nx-lut-lite
OPS=$PKG/patch-fileops
DEST=$task_root/opt/usr/nx-lut-lite
PARENT=$task_root/opt/usr
BACKMOD=$PARENT/nx-lut-lite.before-demo49
META=$PARENT/nx-lut-demo49-backup
REMOVED=$PARENT/nx-lut-lite.demo49-uninstalled
BT=$task_root/usr/sbin/bluetoothd
BTBACK=$task_root/usr/sbin/bluetoothd.nxl-original
UNIT=$task_root/etc/systemd/system/nx-lut-lite.service
RUN=$task_root/tmp/nx-lut-lite
LOCK=$task_root/tmp/nx-lut-demo-setup.lock
# PID values repeat after a camera restart. Select a free transaction suffix
# while holding the existing setup lock; retain every old recovery directory.
set_transaction_paths() {
    WORK=$task_root/tmp/nx-lut-demo-work.$transaction_suffix
    STAGE=$PARENT/.nx-lut-demo-stage.$transaction_suffix
    CURRENT=$PARENT/.nx-lut-demo-current.$transaction_suffix
    BTPREV=$BT.demo49-previous.$transaction_suffix
    BTNEW=$BT.demo49-new.$transaction_suffix
    METAOLD=$PARENT/.nx-lut-demo-meta.$transaction_suffix
}
select_transaction_paths() {
    transaction_attempt=0
    while [ "$transaction_attempt" -lt 128 ];do
        transaction_suffix=$$
        [ "$transaction_attempt" = 0 ] || transaction_suffix=$$.$transaction_attempt
        set_transaction_paths
        transaction_clear=1
        for transaction_path in "$WORK" "$STAGE" "$STAGE.failed" "$CURRENT" "$BTPREV" "$BTNEW" "$METAOLD";do
            if [ -e "$transaction_path" ] || [ -L "$transaction_path" ];then transaction_clear=0;break;fi
        done
        [ "$transaction_clear" = 0 ] || return 0
        transaction_attempt=$((transaction_attempt+1))
    done
    return 1
}
transaction_suffix=$$
set_transaction_paths
MANIFEST_SHA=a02357db72ba2306d6776efe64dc0986ec260fc9348eac7cff5edbf9b2cd1e69
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
OWNERSHIP_INDEX=$PKG/ownership/index.sha256
OWNERSHIP_INDEX_SHA=0000000000000000000000000000000000000000000000000000000000000000
locked=0 work_created=0 stage_created=0 current_moved=0 stage_promoted=0
bt_parked=0 btnew_owned=0 stock_saved=0 stock_restored=0 meta_created=0 meta_parked=0
module_backup_created=0 unit_removed=0 unit_from_backup=0 done=0 helper_ok=0
m1= m2= m3= rw1=0 rw2=0 rw3=0 orig1=0 orig2=0 orig3=0
# The SD root is an OEM directory alias. Only create the fixed log children;
# refuse a linked store/log directory or log leaf instead of following it.
for install_log_dir in "$SD/NXDEMO51" "$SD/NXDEMO51/logs";do
    [ ! -L "$install_log_dir" ] || { printf 'INSTALL_ERROR: linked log directory refused\n' >&2;exit 1; }
    [ ! -e "$install_log_dir" ] || [ -d "$install_log_dir" ] || { printf 'INSTALL_ERROR: non-directory log path refused\n' >&2;exit 1; }
    mkdir -p "$install_log_dir" || exit 1
done
LOG=$SD/NXDEMO51/logs/install.log
[ ! -L "$LOG" ] && { [ ! -e "$LOG" ] || [ -f "$LOG" ]; } || { printf 'INSTALL_ERROR: unsafe install log refused\n' >&2;exit 1; }
exec >> "$LOG" 2>&1
printf 'NXDEMO51 action=%s version=0.4.0-demo51-422\n' "$ACTION"
printf 'workflow=DURABLE_SESSION_RAW422_V1\nENGINE_PROTOCOL=0.3.6-demo50\ninstall_revision=recovery-r10k-ep-flow\n'
phase=INSTALL_PREFLIGHT
failure_reason=UNEXPECTED_EXIT
# Fixed fallback operates before the payload manifest has been verified.
nxl_progress(){ printf 'PROGRESS phase=%s state=%s detail=%s\n' "$1" "$2" "$3";return 0; }

nxl_progress "$phase" RUNNING 'Checking deployment prerequisites'
die(){ failure_reason=$1;printf 'INSTALL_ERROR: %s\n' "$1";exit 1; }
hash(){ "$OPS" sha256 "$1"; }
# Family identity and installed immutable-file receipt, independent of README.
# Old published modules are bridged by their complete original manifests.
verify_owned_manifest() {
    module_verify_root=$1;module_verify_list=$2;module_verify_count=0
    [ -f "$module_verify_list" ] && [ ! -L "$module_verify_list" ] || return 1
    while IFS=' ' read -r module_want module_file module_extra;do
        [ -z "$module_extra" ] && [ "${#module_want}" = 64 ] || return 1
        case "$module_want" in *[!0-9a-f]*) return 1;;esac
        case "$module_file" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) return 1;;esac
        module_verify_count=$((module_verify_count+1));[ "$module_verify_count" -le 256 ] || return 1
        module_component=$module_verify_root;module_old_ifs=$IFS;IFS=/;set -- $module_file;IFS=$module_old_ifs
        for module_part in "$@";do
            [ -n "$module_part" ] && [ "$module_part" != . ] || return 1
            module_component=$module_component/$module_part;[ ! -L "$module_component" ] || return 1
        done
        [ -f "$module_component" ] && [ "$(hash "$module_component")" = "$module_want" ] || return 1
    done < "$module_verify_list"
    [ "$module_verify_count" -ge 20 ]
}
owned_module(){
    module_candidate=$1
    [ -d "$module_candidate" ] && [ ! -L "$module_candidate" ] &&
    [ -f "$module_candidate/lut.sh" ] && [ ! -L "$module_candidate/lut.sh" ] &&
    grep -q '^# NX_LUT_DYNAMIC24_V1$' "$module_candidate/lut.sh" || return 1
    if [ -e "$module_candidate/MODULE.id" ] || [ -L "$module_candidate/MODULE.id" ];then
        [ -f "$module_candidate/MODULE.id" ] && [ ! -L "$module_candidate/MODULE.id" ] &&
        [ -d "$module_candidate/state" ] && [ ! -L "$module_candidate/state" ] &&
        [ -f "$module_candidate/state/demo51-installed" ] && [ ! -L "$module_candidate/state/demo51-installed" ] &&
        grep -qx NXDEMO51_INSTALLED_V1 "$module_candidate/state/demo51-installed" &&
        "$OPS" same "$module_candidate/MODULE.id" "$SRC/MODULE.id" &&
        verify_owned_manifest "$module_candidate" "$module_candidate/state/module.manifest" || return 1
        printf 'MODULE_OWNER_VERIFIED method=INSTALLED_MANIFEST path=%s\n' "$module_candidate"
        return 0
    fi
    if [ -d "$module_candidate/state" ] && [ ! -L "$module_candidate/state" ] &&
       [ -f "$module_candidate/state/demo51-installed" ] && [ ! -L "$module_candidate/state/demo51-installed" ] &&
       grep -qx NXDEMO51_INSTALLED_V1 "$module_candidate/state/demo51-installed";then
        [ -f "$OWNERSHIP_INDEX" ] && [ ! -L "$OWNERSHIP_INDEX" ] && [ "$(hash "$OWNERSHIP_INDEX")" = "$OWNERSHIP_INDEX_SHA" ] || return 1
        while IFS=' ' read -r owner_manifest_hash owner_manifest_name owner_extra;do
            [ -z "$owner_extra" ] && [ "${#owner_manifest_hash}" = 64 ] || return 1
            case "$owner_manifest_name" in ''|*[!A-Za-z0-9_-]*.manifest) return 1;;*.manifest) :;;*) return 1;;esac
            case "$owner_manifest_name" in */*|*..*) return 1;;esac
            owner_manifest=$PKG/ownership/$owner_manifest_name
            [ -f "$owner_manifest" ] && [ ! -L "$owner_manifest" ] && [ "$(hash "$owner_manifest")" = "$owner_manifest_hash" ] || return 1
            if verify_owned_manifest "$module_candidate" "$owner_manifest";then
                printf 'MODULE_OWNER_VERIFIED method=PUBLISHED_MANIFEST release=%s path=%s\n' "$owner_manifest_name" "$module_candidate"
                return 0
            fi
        done < "$OWNERSHIP_INDEX"
        # Preserve the previously supported R5/R6 bridge unchanged.
        [ -f "$module_candidate/README_zh.md" ] && [ ! -L "$module_candidate/README_zh.md" ] || return 1
        case "$(hash "$module_candidate/README_zh.md")" in
            a567c94527b82954af6c36eaf87321c2457feb9b10a49e4be36a4eb45dcad01d|ca29b3e48ffe458ed57c356b791989fe5f421fdeb7be4fedd9d77556b3fe4485) return 0;;
            *) return 1;;
        esac
    fi
    # Older pre-NX51 public modules did not have an installation receipt.
    [ ! -e "$module_candidate/state/demo51-installed" ] && [ ! -L "$module_candidate/state" ] &&
    [ -f "$module_candidate/README_zh.md" ] && [ ! -L "$module_candidate/README_zh.md" ] &&
    grep -q 'NX LUT Lite' "$module_candidate/README_zh.md"
}
module_identity_log() {
    printf 'MODULE_IDENTITY path=%s version_authority=PROGRAM_MANIFEST\n' "$1"
    for module_leaf in MODULE.id README_zh.md lut.sh memory-test.sh state/demo51-installed state/module.manifest;do
        if [ -f "$1/$module_leaf" ] && [ ! -L "$1/$module_leaf" ];then
            printf 'MODULE_IDENTITY_LEAF leaf=%s sha256=%s\n' "$module_leaf" "$(hash "$1/$module_leaf")"
        else printf 'MODULE_IDENTITY_LEAF leaf=%s unavailable_or_linked=1\n' "$module_leaf";fi
    done
}

demo_module(){ [ -f "$1/state/demo49-installed" ] && [ ! -L "$1/state/demo49-installed" ] && grep -qx 'NXDEMO49_INSTALLED_V1' "$1/state/demo49-installed"; }
owned_hook(){ [ -f "$BT" ] && [ ! -L "$BT" ] && grep -q '^# NX_LUT_LITE_BT_HOOK_V1$' "$BT"; }
demo_hook(){ owned_hook && { grep -q '^# NX_LUT_LITE_DEMO51_BT_HOOK_V1$' "$BT" || grep -q '^# NX_LUT_LITE_DEMO49_BT_HOOK_V1$' "$BT"; }; }
# Only this narrowly identified R3 false-path hold permits replacing idle
# control-plane files. A hold is NEVER removed, downgraded or treated as proof
# of hardware cleanup by installation. The explicit runtime path decides later.
known_front_path_hold() {
    hold_dir=$1
    expected_hold_reason=${2:-FRONT_RESTART_OR_PATH_INVALID}
    [ "$ACTION" = install ] && [ -d "$hold_dir" ] && [ ! -L "$hold_dir" ] || return 1
    [ ! -e "$hold_dir/pid" ] && [ ! -L "$hold_dir/pid" ] || return 1
    for hold_leaf in NX51_OWNER RESTART_REQUIRED;do
        [ -f "$hold_dir/$hold_leaf" ] && [ ! -L "$hold_dir/$hold_leaf" ] || return 1
    done
    for hold_entry in "$hold_dir"/* "$hold_dir"/.[!.]* "$hold_dir"/..?*;do
        [ -e "$hold_entry" ] || [ -L "$hold_entry" ] || continue
        case "${hold_entry##*/}" in NX51_OWNER|RESTART_REQUIRED) :;;*) return 1;;esac
    done
    hold_head= hold_jobline= hold_bootline= hold_extra=
    { IFS= read -r hold_head && IFS= read -r hold_jobline && IFS= read -r hold_bootline && ! IFS= read -r hold_extra && [ -z "$hold_extra" ]; } < "$hold_dir/NX51_OWNER" || return 1
    [ "$hold_head" = NX51LOCK1 ] || return 1
    case "$hold_jobline" in "job=$SD/NXDEMO51/JOB_"*) :;;*) return 1;;esac
    hold_job=${hold_jobline#job=};hold_name=${hold_job##*/}
    [ "$hold_job" = "$SD/NXDEMO51/$hold_name" ] && [ "${#hold_name}" -le 160 ] || return 1
    case "$hold_name" in JOB_?*) :;;*) return 1;;esac
    case "$hold_name" in *[!A-Za-z0-9_-]*) return 1;;esac
    [ -d "$SD/NXDEMO51" ] && [ ! -L "$SD/NXDEMO51" ] && [ -d "$hold_job" ] && [ ! -L "$hold_job" ] || return 1
    case "$hold_bootline" in boot=?*) :;;*) return 1;;esac
    hold_boot=${hold_bootline#boot=}
    [ "${#hold_boot}" -le 80 ] || return 1
    case "$hold_boot" in *[!A-Za-z0-9_-]*) return 1;;esac
    hold_head= hold_job2= hold_boot2= hold_reason= hold_extra=
    { IFS= read -r hold_head && IFS= read -r hold_job2 && IFS= read -r hold_boot2 && IFS= read -r hold_reason && ! IFS= read -r hold_extra && [ -z "$hold_extra" ]; } < "$hold_dir/RESTART_REQUIRED" || return 1
    [ "$hold_head" = NX51HOLD1 ] && [ "$hold_job2" = "$hold_jobline" ] && [ "$hold_boot2" = "$hold_bootline" ] && [ "$hold_reason" = "reason=$expected_hold_reason" ] || return 1
    if [ -n "$known_hold_job" ];then [ "$known_hold_job" = "$hold_job" ] && [ "$known_hold_boot" = "$hold_boot" ] || return 1
    else known_hold_job=$hold_job;known_hold_boot=$hold_boot;fi
    return 0
}
# Read-only, installer-only lock diagnostics. This helper never releases locks.
HOLDCHECK=$PKG/bin/nxl-holdcheck.arm
HOLDCHECK_SHA=0118e7de832ee1b0821e694d0b743e3b92c1e33bf9d1d857337cd026d532819d
SESSION_CHECK=$PKG/bin/nxl-session.arm
SESSION_CHECK_SHA=c0708e41cdf303c25eb9c0097362914842be77fb8438b3cbe0a19fa7c738e056
run_holdcheck() {
    [ -f "$HOLDCHECK" ] && [ ! -L "$HOLDCHECK" ] && [ -x "$HOLDCHECK" ] && [ "$(hash "$HOLDCHECK")" = "$HOLDCHECK_SHA" ] || die 'Installation hold helper identity mismatch'
    hold_report=$SD/NXDEMO51/logs/install-holds.log
    hold_temp=$SD/NXDEMO51/logs/.install-holds.$$.tmp
    [ ! -L "$hold_report" ] && { [ ! -e "$hold_report" ] || [ -f "$hold_report" ]; } || die 'Unsafe installation hold report path'
    [ ! -e "$hold_temp" ] && [ ! -L "$hold_temp" ] || die 'Installation hold report temporary path occupied'
    printf 'INSTALL_HOLD_CHECK revision=recovery-r10k-ep-flow phase=%s operation=%s\n' "$phase" "$1" >> "$hold_report"
    hold_rc=0
    (umask 077; set -C; "$HOLDCHECK" "$1" > "$hold_temp" 2>&1) || hold_rc=$?
    [ -f "$hold_temp" ] && [ ! -L "$hold_temp" ] || die 'Installation hold diagnostic failed to create report'
    while IFS= read -r hold_line;do
        printf '%s\n' "$hold_line"
        printf '%s\n' "$hold_line" >> "$hold_report"
    done < "$hold_temp"
    rm -f "$hold_temp"
    return "$hold_rc"
}
init_hold_observed=0 init_hold_sha=
require_init_hold_unchanged() {
    init_hold_file=$RUN/auto-init-held
    init_hold_current=
    if [ -e "$init_hold_file" ] || [ -L "$init_hold_file" ];then
        # Updating immutable program files is separate from authorizing an ISP
        # operation. Keep the unresolved reference record byte-for-byte; the
        # installed runtime must continue to refuse every new initialization.
        [ "$ACTION" = install ] || die 'An initialization return is unresolved; no rollback or uninstall'
        [ -f "$init_hold_file" ] && [ ! -L "$init_hold_file" ] || die 'Unsafe initialization hold path; no hold changed'
        init_hold_current=$(hash "$init_hold_file") || die 'Initialization hold unreadable; no hold changed'
        [ "${#init_hold_current}" = 64 ] || die 'Initialization hold digest unavailable'
    fi
    if [ "$init_hold_observed" = 0 ];then
        init_hold_observed=1;init_hold_sha=$init_hold_current
    else
        [ "$init_hold_current" = "$init_hold_sha" ] || die 'Initialization hold changed during installation; no hold changed by installer'
    fi
    if [ -n "$init_hold_current" ];then
        printf 'INIT_HOLD_PRESERVED sha256=%s; INSTALL_CONTROL_PLANE_ONLY; NATIVE_CALLS=0; INIT_RUNTIME_STAYS_BLOCKED\n' "$init_hold_sha"
    fi
}
require_idle_workers() {
    if nxl_process_running nxl-menu nxl-demo-front nxl-demo-front. nxl-demo-post nxl-demo-post.a nxl-allocprobe. nxl-session.arm nxl-hw-controll nxl-hw-worker.a nxl-bitbworker. nxl-oem-restore nxl-init-test.a nxl-init-pipeli nxl-owned-ep.ar nxl-lens-watch. nxl-video-mode. nxl-video-fullw nxl-video-probe nxl-usb-guard nxl-usb-guard.a;then die 'Close menu and stop test recorders before deploying'
    else [ "$?" = 1 ] || die 'Process view unavailable';fi
    require_init_hold_unchanged
}
require_install_holds() {
    held_count=0
    for held_name in engine.lock trace.lock memory-test.lock demo50-recovery.lock;do
        [ -e "$RUN/$held_name" ] || [ -L "$RUN/$held_name" ] || continue
        held_count=$((held_count+1))
    done
    [ "$held_count" != 0 ] || return 0
    run_holdcheck --inventory || die 'Installation hold inventory unavailable; no hold changed'
    if [ "$ACTION" = install ] && run_holdcheck --check-nx51-pair;then
        printf 'OWNED_NX51_HOLDS_PRESERVED; INSTALL_CONTROL_PLANE_ONLY; RESOURCE_CHECK_REQUIRED; NO_HARDWARE_IDLE_CLAIM\n'
        return 0
    fi
    if [ "$ACTION" = install ] && run_holdcheck --check-oem-pair;then
        printf 'KNOWN_OEM_RESTORE_HOLDS_PRESERVED; INSTALL_CONTROL_PLANE_ONLY; NO_HARDWARE_IDLE_CLAIM\n'
        return 0
    fi
    die 'Active, foreign or malformed hold blocks installation; no hold changed'
}

safe_state_tree(){
    [ "$2" -le 16 ] && [ -d "$1" ] && [ ! -L "$1" ] || return 1
    for state_node in "$1"/* "$1"/.[!.]* "$1"/..?*;do
        [ -e "$state_node" ] || [ -L "$state_node" ] || continue
        [ ! -L "$state_node" ] || return 1
        if [ -d "$state_node" ];then safe_state_tree "$state_node" "$(( $2 + 1 ))" || return 1
        else [ -f "$state_node" ] || return 1;fi
    done
}
unit_mode(){
    case "$1" in enabled) systemctl enable nx-lut-lite.service;;disabled) systemctl disable nx-lut-lite.service;;static) :;;*) return 1;;esac
}
mount_rw(){
    info=$("$OPS" mount-info "$task_root/proc/mounts" "$1") || die 'Cannot identify covering mount'
    set -- $info;[ "$#" = 2 ] || die 'Invalid covering mount description'
    point=$1;mode=$2
    case "$point" in /*) :;;*) die 'Invalid covering mount path';;esac
    case "$mode" in ro|rw) :;;*) die 'Invalid covering mount mode';;esac
    [ "$point" != "$m1" ] && [ "$point" != "$m2" ] && [ "$point" != "$m3" ] || return 0
    if [ -z "$m1" ];then m1=$point;slot=1
    elif [ -z "$m2" ];then m2=$point;slot=2
    elif [ -z "$m3" ];then m3=$point;slot=3
    else die 'Too many covering mounts';fi
    if [ "$mode" = ro ];then
        case "$slot" in 1) rw1=1;orig1=1;;2) rw2=1;orig2=1;;3) rw3=1;orig3=1;;esac
        mount -o remount,rw "$point" || die 'Cannot remount target writable'
    fi
}
restore_mounts(){
    bad=0
    if [ "$rw3" = 1 ];then mount -o remount,ro "$m3" && rw3=0 || bad=1;fi
    if [ "$rw2" = 1 ];then mount -o remount,ro "$m2" && rw2=0 || bad=1;fi
    if [ "$rw1" = 1 ];then mount -o remount,ro "$m1" && rw1=0 || bad=1;fi
    return "$bad"
}
finish(){
    rc=$?;set +e;trap - 0 HUP INT TERM
    if [ "$done" != 1 ];then
        recovered=1
        if [ "$orig1" = 1 ];then rw1=1;mount -o remount,rw "$m1" || recovered=0;fi
        if [ "$orig2" = 1 ];then rw2=1;mount -o remount,rw "$m2" || recovered=0;fi
        if [ "$orig3" = 1 ];then rw3=1;mount -o remount,rw "$m3" || recovered=0;fi
        if [ "$stage_promoted" = 1 ] && [ -d "$DEST" ];then
            if [ "$ACTION" = rollback ];then
                [ -e "$BACKMOD" ] || mv "$DEST" "$BACKMOD" || recovered=0
            elif [ ! -e "$STAGE" ];then mv "$DEST" "$STAGE.failed" || recovered=0;fi
        fi
        if [ "$current_moved" = 1 ] && [ -d "$CURRENT" ];then mv "$CURRENT" "$DEST" || recovered=0
        elif [ "$module_backup_created" = 1 ] && [ -d "$BACKMOD" ];then mv "$BACKMOD" "$DEST" || recovered=0;fi
        if [ "$stock_restored" = 1 ] && [ ! -e "$BTBACK" ] && [ -f "$BT" ];then mv "$BT" "$BTBACK" || recovered=0;fi
        if [ "$bt_parked" = 1 ];then
            rm -f "$BT"
            if [ -f "$BTPREV" ];then mv "$BTPREV" "$BT" || recovered=0
            else cp -p "$WORK/bt.previous" "$BT" || recovered=0;fi
        elif [ "$stock_saved" = 1 ] && [ -f "$BTBACK" ];then rm -f "$BT";mv "$BTBACK" "$BT" || recovered=0;fi
        if [ "$unit_from_backup" = 1 ];then systemctl disable nx-lut-lite.service;rm -f "$UNIT";systemctl daemon-reload;fi
        if [ "$unit_removed" = 1 ];then cp -p "$WORK/unit.previous" "$UNIT" && systemctl daemon-reload && unit_mode "$unit_enabled" || recovered=0;fi
        if [ "$meta_created" = 1 ] && [ "$recovered" = 1 ];then rm -rf "$META";fi
        if [ "$meta_parked" = 1 ] && [ "$recovered" = 1 ] && [ -d "$METAOLD" ];then mv "$METAOLD" "$META" || recovered=0;fi
        printf 'INSTALL_FAILED exit=%s restored=%s\n' "$rc" "$recovered"
        nxl_progress "$phase" FAILED "$failure_reason; exit=$rc; rollback_restored=$recovered"
        [ "$recovered" = 1 ] || { rc=1;printf 'RECOVERY_REQUIRED: keep work and backups; cold-restart before further use.\n'; }
    else recovered=1;fi
    [ "$btnew_owned" != 1 ] || rm -f "$BTNEW"
    if [ "$stage_created" = 1 ] && [ "$recovered" = 1 ];then rm -rf "$STAGE" "$STAGE.failed";fi
    # Keep successful module/metadata recovery directories. Their original
    # mount mode has already been restored, so do not mutate them here.
    restore_mounts || { rc=1;printf 'MOUNT_RESTORE_ERROR\n'; }
    if [ "$work_created" = 1 ] && [ "$recovered" = 1 ];then rm -rf "$WORK";fi
    if [ "$locked" = 1 ];then
        if [ "$helper_ok" = 1 ];then "$OPS" unlock "$LOCK" || rc=1;else rmdir "$LOCK" || rc=1;fi
        if [ -e "$LOCK" ] || [ -L "$LOCK" ];then rc=1;printf 'SETUP_LOCK_VISIBLE_AFTER_RELEASE: no path will be deleted again; cold-restart before retry.\n';fi
    fi
    sync
    if [ "$done" = 1 ] && [ "$rc" = 0 ];then printf 'INSTALL_COMPLETE action=%s; POWER_OFF_ON_THEN_BLUETOOTH; no boot identifier check.\n' "$ACTION";nxl_progress INSTALL_COMPLETE DONE "action=$ACTION; power off/on then Bluetooth; no ISP started";fi
    exit "$rc"
}
trap finish 0
trap 'exit 130' HUP INT TERM
case "$ACTION" in install|uninstall|rollback) :;;*) die 'Unsupported action';;esac
for utility in cp mv rm mkdir rmdir chmod grep mount sync sleep;do command -v "$utility" >/dev/null 2>&1 || die "Missing utility: $utility";done
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] || die 'Missing bundled file utility'
[ "$(hash "$OPS")" = "$OPS_SHA" ] || die 'Bundled file utility identity mismatch'
helper_ok=1
"$OPS" version "$task_root/etc/version.info" || die 'Requires NX500 firmware 1.12'
mkdir "$LOCK" 2>/dev/null || die 'Another deployment is active; no lock reclaimed'
locked=1
mkdir -p "$RUN"
"$OPS" volatile-fs "$RUN" || die 'Runtime path is not a verified volatile filesystem'
[ ! -e "$task_root/tmp/nx-isp-full-capture.lock" ] && [ ! -L "$task_root/tmp/nx-isp-full-capture.lock" ] && [ ! -e "$SD/NXISP_FULL_ARM" ] && [ ! -L "$SD/NXISP_FULL_ARM" ] || die 'Other ISP trace is active or armed; disarm it before deployment'
[ ! -e "$RUN/session/video_enabled" ] && [ ! -e "$RUN/session/video_pending" ] || die 'Disable video LUT before deployment'
phase=VERIFY_PAYLOAD
nxl_progress "$phase" RUNNING 'Checking complete module manifest'
case "$MANIFEST_SHA" in *@*|'') die 'Unrendered deployment manifest';;esac
[ -f "$PKG/module.manifest" ] && [ ! -L "$PKG/module.manifest" ] && [ "$(hash "$PKG/module.manifest")" = "$MANIFEST_SHA" ] || die 'Module manifest identity mismatch'
while IFS=' ' read -r want file extra;do
    [ -z "$extra" ] && [ "${#want}" = 64 ] || die 'Malformed module manifest'
    case "$want" in *[!0-9a-f]*) die 'Malformed module digest';;esac
    case "$file" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) die 'Unsafe module manifest path';;esac
    [ -f "$SRC/$file" ] && [ ! -L "$SRC/$file" ] && [ "$(hash "$SRC/$file")" = "$want" ] || die "Module identity mismatch: $file"
done < "$PKG/module.manifest"
# Payload code is used only after every manifest entry has matched.
if [ -f "$SRC/progress.sh" ] && [ ! -L "$SRC/progress.sh" ];then
    NXL_PROGRESS_SD=$SD;NXL_PROGRESS_WRITER=$SRC/bin/nxl-session.arm;export NXL_PROGRESS_SD NXL_PROGRESS_WRITER
    . "$SRC/progress.sh"
fi
phase=CHECK_RUNTIME
nxl_progress "$phase" RUNNING 'Checking active workers and owned Bluetooth hook'
command -v prefman >/dev/null 2>&1 || die 'Missing prefman'
[ -r "$task_root/lib/ld-linux.so.3" ] || die 'Missing camera ARM loader'
[ -r "$SRC/process.sh" ] && [ ! -L "$SRC/process.sh" ] || die 'Missing deployment process helper'
. "$SRC/process.sh"
[ -f "$HOLDCHECK" ] && [ ! -L "$HOLDCHECK" ] && [ -x "$HOLDCHECK" ] && [ "$(hash "$HOLDCHECK")" = "$HOLDCHECK_SHA" ] || die 'Installation hold helper identity mismatch'
require_idle_workers
require_install_holds
nxl_require_no_keyscan || die 'Legacy keyscan detected; no hook overwritten'
[ ! -e "$task_root/opt/usr/nx-ks/keyscan" ] && [ ! -e "$task_root/usr/sbin/bluetoothd.orig" ] || die 'Unowned legacy mod present'
[ ! -L "$DEST" ] && [ ! -L "$PARENT" ] && [ ! -L "$BT" ] && [ ! -L "$BTBACK" ] && [ ! -L "$META" ] && [ ! -L "$BACKMOD" ] || die 'Symlinked deployment path refused'
had_module=0;had_hook=stock;had_unit=0;unit_enabled=disabled;unit_active=inactive
if [ -e "$DEST" ];then module_identity_log "$DEST";owned_module "$DEST" || die 'Unknown existing module';had_module=1;fi
if owned_hook;then had_hook=owned;[ -x "$BTBACK" ] && [ "$("$OPS" kind "$BTBACK")" = elf ] || die 'Owned hook has no verified original backup'
else
    [ "$("$OPS" kind "$BT")" = elf ] || die 'Unknown Bluetooth hook refused'
    [ ! -e "$BTBACK" ] || die 'Unexpected Bluetooth backup refused'
fi
if [ -e "$UNIT" ];then
    [ -f "$UNIT" ] && [ ! -L "$UNIT" ] && grep -q '^Description=NX LUT Lite' "$UNIT" || die 'Unknown service refused'
    command -v systemctl >/dev/null 2>&1 || die 'systemctl required to migrate our old service';had_unit=1
    unit_rc=0;unit_enabled=$(systemctl is-enabled nx-lut-lite.service 2>/dev/null) || unit_rc=$?
    case "$unit_enabled:$unit_rc" in enabled:0|disabled:*|static:*) :;;*) die 'Cannot preserve previous service enablement';;esac
    unit_active=$(systemctl is-active nx-lut-lite.service 2>/dev/null) || :
    case "$unit_active" in active|inactive|failed|activating|deactivating) :;;*) die 'Cannot record previous service state';;esac
fi
select_transaction_paths || die 'No unused recovery transaction paths available; old backups preserved'
mkdir "$WORK" || die 'Recovery work directory already exists';work_created=1
for unused in "$CURRENT" "$BTPREV" "$BTNEW" "$METAOLD" "$STAGE.failed";do
    [ ! -e "$unused" ] && [ ! -L "$unused" ] || die 'A retained recovery path already exists; refusing overwrite'
done
if [ "$had_hook" = owned ];then cp -p "$BT" "$WORK/bt.previous";fi
if [ "$had_unit" = 1 ];then cp -p "$UNIT" "$WORK/unit.previous";fi
if [ "$ACTION" = install ];then
    [ -f "$PKG/assets/SAM_0183.SRW" ] && [ ! -L "$PKG/assets/SAM_0183.SRW" ] || die 'Fixed SAM_0183 source missing'
    [ "$(hash "$PKG/assets/SAM_0183.SRW")" = dfd6608e4ee05f0d5b4b402fedd3c928aee509595e6964efa4c1ce87f1d9715c ] || die 'Existing SAM_0183 source hash mismatch; retain verified R4 assets'
fi
phase=OPEN_FILESYSTEM
nxl_progress "$phase" RUNNING 'Preparing writable install targets'
mount_rw "$PARENT";mount_rw "$BT";[ "$had_unit" != 1 ] || mount_rw "$UNIT"
if [ "$ACTION" = install ];then
    phase=STAGE_MODULE
    nxl_progress "$phase" RUNNING 'Copying and verifying complete module'
    mkdir "$STAGE" || die 'Module staging path already exists';stage_created=1
    cp -R "$SRC/." "$STAGE/"
    while IFS=' ' read -r want file extra;do [ "$(hash "$STAGE/$file")" = "$want" ] || die 'Staged module verification failed';done < "$PKG/module.manifest"
    mkdir -p "$STAGE/state"
    if [ "$had_module" = 1 ];then
        [ ! -L "$DEST/state" ] || die 'Existing state directory symlink refused'
        if [ -d "$DEST/state" ];then safe_state_tree "$DEST/state" 0 || die 'Existing state links or special files refused';cp -R "$DEST/state/." "$STAGE/state/";fi
    fi
    safe_state_tree "$STAGE/state" 0 || die 'Copied state links or special files refused'
    cp "$PKG/module.manifest" "$STAGE/state/module.manifest" || die 'Cannot save installed module receipt'
    "$OPS" same "$PKG/module.manifest" "$STAGE/state/module.manifest" || die 'Installed module receipt verification failed'
    rm -f "$STAGE/state/disabled" "$STAGE/state/demo49-installed"
    # Keep the previous family identity solely for backup/uninstall compatibility.
    printf 'NXDEMO49_INSTALLED_V1\n' > "$STAGE/state/demo49-installed"
    printf 'NXDEMO51_INSTALLED_V1\nversion=0.4.0-demo51-422\ninstall_revision=recovery-r10k-ep-flow\n' > "$STAGE/state/demo51-installed"
    if [ ! -f "$STAGE/state/p49-experiment.state" ];then
        if [ "$held_count" = 0 ];then printf 'NXP33INSTALL1\nREADY=0.3.6-demo50\n' > "$STAGE/state/p49-experiment.state"
        else printf 'NXP33INSTALL1\nSTARTED=0.3.6-demo50\npid=0\nreport=INSTALL_CONTROL_PLANE_ONLY\n' > "$STAGE/state/p49-experiment.state";fi
    fi
    chmod 600 "$STAGE/state/p49-experiment.state"
    printf 'INSTALLED_POWER_OFF_ON_THEN_BLUETOOTH_NO_BOOT_ID_CHECK\n' > "$STAGE/state/startup.status"
    if [ "$had_module" = 1 ] && demo_module "$DEST";then
        [ -f "$META/identity" ] && grep -qx NXDEMO49_BACKUP_V1 "$META/identity" || die 'Missing first-install recovery metadata'
    else
        if [ -e "$META" ];then [ -d "$META" ] && [ ! -e "$BACKMOD" ] || die 'Existing backup would be overwritten';meta_parked=1;mv "$META" "$METAOLD";fi
        [ ! -e "$BACKMOD" ] || die 'Original module backup already exists'
        mkdir "$META";meta_created=1
        printf 'NXDEMO49_BACKUP_V1\n' > "$META/identity"
        printf '%s\n' "$had_module" > "$META/had-module"
        printf '%s\n' "$had_hook" > "$META/had-hook"
        printf '%s\n' "$had_unit" > "$META/had-unit"
        if [ "$had_hook" = owned ];then cp -p "$BT" "$META/bluetoothd.previous";fi
        if [ "$had_unit" = 1 ];then cp -p "$UNIT" "$META/unit.previous";printf '%s\n' "$unit_enabled" > "$META/unit-enabled.previous";printf '%s\n' "$unit_active" > "$META/unit-active.previous";fi
    fi
    btnew_owned=1;cp "$SRC/bluetoothd-wrapper.sh" "$BTNEW";chmod 755 "$BTNEW";/bin/sh -n "$BTNEW"
else
    [ "$had_module" = 1 ] && demo_module "$DEST" && demo_hook || die 'Demo module and owned hook required'
    [ "$ACTION" != rollback ] || { [ -f "$META/identity" ] && grep -qx NXDEMO49_BACKUP_V1 "$META/identity" && [ -d "$BACKMOD" ] && owned_module "$BACKMOD"; } || die 'Original module rollback unavailable'
    if [ "$ACTION" = rollback ];then
        IFS= read -r previous_hook < "$META/had-hook"
        case "$previous_hook" in owned) btnew_owned=1;cp -p "$META/bluetoothd.previous" "$BTNEW";;stock) :;;*) die 'Invalid rollback hook metadata';;esac
    fi
fi
phase=SWITCH_COMPONENTS
nxl_progress "$phase" RUNNING 'Installing module and Bluetooth wrapper'
# Only the project's own listener/service is stopped; stock Bluetooth remains.
nxl_stop_listener
n=0
while :;do
    if nxl_process_running nxl-keys;then
        [ "$n" -lt 5 ] || die 'Listener did not stop; no component switched'
        n=$((n+1));sleep 1
    else [ "$?" = 1 ] || die 'Cannot verify listener stopped';break;fi
done
# Listener is now stopped; refuse any worker or changed hold before commit.
require_idle_workers
require_install_holds
if [ "$had_unit" = 1 ];then unit_removed=1;systemctl stop nx-lut-lite.service;systemctl disable nx-lut-lite.service;rm -f "$UNIT";systemctl daemon-reload;fi
if [ "$had_module" = 1 ];then
    if [ "$ACTION" = install ] && ! demo_module "$DEST";then module_backup_created=1;mv "$DEST" "$BACKMOD"
    else current_moved=1;mv "$DEST" "$CURRENT";fi
fi
if [ "$ACTION" = install ];then
    stage_promoted=1;mv "$STAGE" "$DEST"
    owned_module "$DEST" || die 'Installed private module readback failed'
    printf 'INSTALL_RUNTIME_COHERENT root=%s module_manifest_sha256=%s private_executables=4\n' "$DEST" "$MANIFEST_SHA"
    if [ "$had_hook" = stock ];then stock_saved=1;mv "$BT" "$BTBACK"
    else bt_parked=1;mv "$BT" "$BTPREV";fi
    mv "$BTNEW" "$BT"
    demo_hook && demo_module "$DEST" || die 'Committed demo identity not verified'
    printf 'NXDEMO49INSTALL1\n' > "$RUN/demo49-install-session"
elif [ "$ACTION" = rollback ];then
    bt_parked=1;mv "$BT" "$BTPREV"
    if [ "$previous_hook" = owned ];then mv "$BTNEW" "$BT";else stock_restored=1;mv "$BTBACK" "$BT";fi
    stage_promoted=1;mv "$BACKMOD" "$DEST"
else
    bt_parked=1;mv "$BT" "$BTPREV"
    stock_restored=1;mv "$BTBACK" "$BT"
fi
if [ "$ACTION" = rollback ];then
    IFS= read -r old_unit < "$META/had-unit"
    case "$old_unit" in
        0) :;;
        1) command -v systemctl >/dev/null 2>&1 || die 'systemctl required for previous service';IFS= read -r previous_enabled < "$META/unit-enabled.previous";unit_from_backup=1;cp -p "$META/unit.previous" "$UNIT";systemctl daemon-reload;unit_mode "$previous_enabled";printf 'PREVIOUS_SERVICE_RESTORED=%s; cold-restart to resume if previously active.\n' "$previous_enabled";;
        *) die 'Invalid rollback service metadata';;
    esac
fi
if [ "$bt_parked" = 1 ];then "$OPS" same "$BTPREV" "$WORK/bt.previous" || die 'Bluetooth recovery copy differs';rm -f "$BTPREV";fi
if [ "$current_moved" = 1 ];then printf 'PREVIOUS_DEMO_RETAINED=%s\n' "$CURRENT";fi
if [ "$meta_parked" = 1 ];then printf 'PREVIOUS_METADATA_RETAINED=%s\n' "$METAOLD";fi
if [ "$ACTION" = install ];then
    mkdir -p "$SD/looks"
    [ ! -L "$SD/looks" ] || die 'SD looks directory symlink refused'
    for look in "$SRC"/looks/*.bin;do
        [ -f "$look" ] || continue
        [ ! -L "$look" ] || die 'Bundled look symlink refused'
        name=${look##*/}
        if [ ! -e "$SD/looks/$name" ];then
            cp "$look" "$SD/looks/.demo49-$name.$$"
            "$OPS" same "$look" "$SD/looks/.demo49-$name.$$" || die 'Bundled look copy differs'
            mv "$SD/looks/.demo49-$name.$$" "$SD/looks/$name"
        fi
    done
fi
sync
# Recheck the protected record after readback as well as before promotion.
require_init_hold_unchanged
# Verify RO restoration before discarding current group; a failed restore rolls
# the transaction back while its paths remain recoverable.
phase=RESTORE_FILESYSTEM
nxl_progress "$phase" RUNNING 'Restoring filesystem mounts and syncing SD'
restore_mounts || die 'Cannot restore original mount mode'
done=1
printf 'INSTALL_ONLY=1\nAUTO_ISP=0\nSESSION_ROOT=/mnt/mmc/NXDEMO51\nUSER_POWER_CYCLE_RECOMMENDED=1\nBOOT_ID_REQUIRED=0\n'
exit 0
