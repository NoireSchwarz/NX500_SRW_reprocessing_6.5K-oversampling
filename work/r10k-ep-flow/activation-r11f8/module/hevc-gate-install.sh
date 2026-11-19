#!/bin/sh
# NXL_HEVC_GATE_INSTALL3 - sourced only after the complete module manifest.
# Retain both old and new inodes: mapped zero-link files block Linux RO remount.
# Never rewrite a mapped inode or remove retained recovery links.
HG_OEM_SHA=b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8
HG_PATCH_SHA=7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9
HG_CORE=$task_root/usr/lib/libmmfcore.so
HG_BACK=$task_root/usr/lib/libmmfcore.so.nxl-r11f-oem
HG_RENAME_SHA=7424ee60dc9947d63789bba534ef96710bbdd13218caafc0e3ba3f7712350a48
HG_RENAME=$SRC/bin/nxl-atomic-rename.arm
hg_native_switched=0
# Native GNU attr tools are present in the pinned1.12 firmware. Keep all
# extended attributes (including SMACK64/SMACK64EXEC) separate from file bytes.
hg_attributes(){
    hg_attr_raw=$(getfattr --absolute-names -d -m - "$1") || return 1
    while IFS= read -r hg_attr_line;do
        case "$hg_attr_line" in \#*|'') :;;*) printf '%s\n' "$hg_attr_line";;esac
    done <<HG_ATTRIBUTE_EOF
$hg_attr_raw
HG_ATTRIBUTE_EOF
}
hg_copy_attributes(){
    hg_source_attrs=$(hg_attributes "$1") || return 1
    hg_attr_file=$WORK/hevc-attributes.restore
    [ ! -L "$hg_attr_file" ] && { [ ! -e "$hg_attr_file" ] || [ -f "$hg_attr_file" ]; } || return 1
    { printf '# file: %s\n' "$2";printf '%s\n' "$hg_source_attrs"; } > "$hg_attr_file" || return 1
    setfattr --restore="$hg_attr_file" || return 1
    hg_target_attrs=$(hg_attributes "$2") || return 1
    [ "$hg_target_attrs" = "$hg_source_attrs" ]
}
hg_native_preflight(){
    [ -f "$HG_RENAME" ] && [ ! -L "$HG_RENAME" ] && [ -x "$HG_RENAME" ] && [ "$(hash "$HG_RENAME")" = "$HG_RENAME_SHA" ] || die 'HEVC gate: atomic rename helper identity mismatch' 
    command -v ln >/dev/null 2>&1 || die 'HEVC gate: hard-link utility required'
    command -v getfattr >/dev/null 2>&1 && command -v setfattr >/dev/null 2>&1 || die 'HEVC gate: native attribute tools required'
    [ -d "$task_root/usr" ] && [ ! -L "$task_root/usr" ] && [ -d "$task_root/usr/lib" ] && [ ! -L "$task_root/usr/lib" ] || die 'HEVC gate: linked library parent refused'
    [ -f "$HG_CORE" ] && [ ! -L "$HG_CORE" ] || die 'HEVC gate: original library missing or linked'
    hg_cover_info=$("$OPS" mount-info "$task_root/proc/mounts" "$HG_CORE") || die 'HEVC gate: cannot identify library filesystem'
    set -- $hg_cover_info
    [ "$#" = 2 ] || die 'HEVC gate: invalid library filesystem description'
    case "$1" in /*) :;;*) die 'HEVC gate: invalid library mount path';;esac
    [ "$2" = ro ] || die 'HEVC gate: library filesystem is not read-only; cold power off/on required before retry'
    printf 'HEVC_GATE_MOUNT_PREFLIGHT point=%s mode=ro retained_inode_protocol=2\n' "$1"
    HG_BEFORE=$(hash "$HG_CORE")
    HG_BEFORE_ATTRS=$(hg_attributes "$HG_CORE") || die 'HEVC gate: library security attributes unreadable' 
    case "$HG_BEFORE" in "$HG_OEM_SHA"|"$HG_PATCH_SHA") :;;*) die 'HEVC gate: unknown library edits preserved';;esac
    if [ -e "$HG_BACK" ] || [ -L "$HG_BACK" ];then
        [ -f "$HG_BACK" ] && [ ! -L "$HG_BACK" ] && [ "$(hash "$HG_BACK")" = "$HG_OEM_SHA" ] || die 'HEVC gate: unknown OEM backup preserved'
    else [ "$HG_BEFORE" = "$HG_OEM_SHA" ] || die 'HEVC gate: patched library has no OEM backup';fi
    [ -f "$SRC/native/libmmfcore.r11f.so" ] && [ ! -L "$SRC/native/libmmfcore.r11f.so" ] && [ "$(hash "$SRC/native/libmmfcore.r11f.so")" = "$HG_PATCH_SHA" ] || die 'HEVC gate: payload mismatch'
    printf 'HEVC_GATE_PREFLIGHT current_sha256=%s OEM_backup_sha256=%s default=PASSTHROUGH live_text_write=0\n' "$HG_BEFORE" "$HG_OEM_SHA"
}
hg_native_prepare(){
    HG_NEW=$HG_CORE.nxl-new.$transaction_suffix
    HG_PREV=$HG_CORE.nxl-before.$transaction_suffix
    HG_STAGED=$HG_CORE.nxl-staged.$transaction_suffix
    HG_RESTORE=$HG_CORE.nxl-restore.$transaction_suffix
    HG_BACKNEW=$HG_BACK.new.$transaction_suffix
    for hg_path in "$HG_NEW" "$HG_PREV" "$HG_STAGED" "$HG_RESTORE" "$HG_BACKNEW";do [ ! -e "$hg_path" ] && [ ! -L "$hg_path" ] || die 'HEVC gate: retained recovery path collision';done
    if [ "$ACTION" = install ];then HG_AFTER=$HG_PATCH_SHA;HG_PAYLOAD=$SRC/native/libmmfcore.r11f.so
    else HG_AFTER=$HG_OEM_SHA;HG_PAYLOAD=$HG_BACK;fi
    [ "$(hash "$HG_CORE")" = "$HG_BEFORE" ] || die 'HEVC gate: library changed during preflight'
}
# GNU coreutils6.9 mv unlinks multiply linked destinations before rename.
# Keep inode retention and bypass that path with a single authenticated rename.
hg_atomic_replace(){
    if "$HG_RENAME" "$1" "$2";then return 0;else hg_rename_rc=$?;fi
    printf 'HEVC_GATE_ATOMIC_RENAME_FAILED helper_exit=%s; no unlink/copy fallback\n' "$hg_rename_rc"
    printf 'HEVC_GATE_FAILURE_MOUNTS_BEGIN\n'
    cat "$task_root/proc/mounts" || :
    printf 'HEVC_GATE_FAILURE_MOUNTS_END\nHEVC_GATE_FAILURE_KERNEL_BEGIN\n'
    if command -v dmesg >/dev/null 2>&1;then dmesg | tail -n 80 || :;fi
    printf 'HEVC_GATE_FAILURE_KERNEL_END\n'
    return "$hg_rename_rc"
}
hg_native_commit(){
    [ "$HG_BEFORE" != "$HG_AFTER" ] || { printf 'HEVC_GATE_LIBRARY already_expected=1 sha256=%s\n' "$HG_AFTER";return 0; }
    mount_rw "$HG_CORE"
    [ "$(hash "$HG_CORE")" = "$HG_BEFORE" ] || die 'HEVC gate: concurrent library change'
    # A byte copy does not keep the currently mapped inode linked. Its last
    # unlink would leave s_remove_count nonzero and prevent remount,ro.
    ln "$HG_CORE" "$HG_PREV" || die 'HEVC gate: cannot retain original inode'
    [ -f "$HG_PREV" ] && [ ! -L "$HG_PREV" ] && [ "$HG_CORE" -ef "$HG_PREV" ] || die 'HEVC gate: original inode retention failed'
    [ "$(hash "$HG_PREV")" = "$HG_BEFORE" ] && [ "$(hg_attributes "$HG_PREV")" = "$HG_BEFORE_ATTRS" ] || die 'HEVC gate: transaction recovery readback failed'
    if [ ! -e "$HG_BACK" ];then
        [ "$HG_BEFORE" = "$HG_OEM_SHA" ] || die 'HEVC gate: no original to back up'
        cp -p "$HG_CORE" "$HG_BACKNEW"
        hg_copy_attributes "$HG_CORE" "$HG_BACKNEW" || die 'HEVC gate: OEM backup attributes differ'
        [ "$(hash "$HG_BACKNEW")" = "$HG_OEM_SHA" ] || die 'HEVC gate: OEM backup readback failed'
        mv "$HG_BACKNEW" "$HG_BACK"
        sync
    fi
    [ -f "$HG_PAYLOAD" ] && [ ! -L "$HG_PAYLOAD" ] && [ "$(hash "$HG_PAYLOAD")" = "$HG_AFTER" ] || die 'HEVC gate: commit source changed'
    cp -p "$HG_CORE" "$HG_NEW"
    [ ! "$HG_NEW" -ef "$HG_CORE" ] && [ ! "$HG_NEW" -ef "$HG_PREV" ] || die 'HEVC gate: staged library aliases a live inode'
    cp "$HG_PAYLOAD" "$HG_NEW"
    hg_copy_attributes "$HG_CORE" "$HG_NEW" || die 'HEVC gate: new library attributes differ' 
    [ "$(hash "$HG_NEW")" = "$HG_AFTER" ] || die 'HEVC gate: staged library readback failed'
    # A process may map the new inode before a later rollback. Retain it too.
    ln "$HG_NEW" "$HG_STAGED" || die 'HEVC gate: cannot retain staged inode'
    [ -f "$HG_STAGED" ] && [ ! -L "$HG_STAGED" ] && [ "$HG_NEW" -ef "$HG_STAGED" ] || die 'HEVC gate: staged inode retention failed'
    printf 'HEVC_GATE_INODES_RETAINED previous=%s staged=%s same_inode_verified=1\n' "$HG_PREV" "$HG_STAGED"
    sync
    hg_native_switched=1
    hg_atomic_replace "$HG_NEW" "$HG_CORE"
    [ "$HG_CORE" -ef "$HG_STAGED" ] && [ "$(hash "$HG_CORE")" = "$HG_AFTER" ] && [ "$(hash "$HG_BACK")" = "$HG_OEM_SHA" ] || die 'HEVC gate: final library readback failed'
    [ "$(hg_attributes "$HG_CORE")" = "$HG_BEFORE_ATTRS" ] || die 'HEVC gate: committed library security attributes differ'
    printf 'HEVC_GATE_LIBRARY_COMMITTED path=%s sha256=%s OEM_backup=%s; COLD_POWER_CYCLE_REQUIRED; live_process_still_uses_previous_inode\n' "$HG_CORE" "$HG_AFTER" "$HG_BACK"
}
hg_native_recover(){
    [ "$hg_native_switched" = 1 ] || return 0
    [ -f "$HG_PREV" ] && [ ! -L "$HG_PREV" ] && [ "$(hash "$HG_PREV")" = "$HG_BEFORE" ] || return 1
    # The commit flag is armed before rename. If rename itself failed, the
    # original inode can still be in place; do not rename a hardlink onto itself.
    if [ -f "$HG_CORE" ] && [ ! -L "$HG_CORE" ] && [ "$HG_CORE" -ef "$HG_PREV" ];then
        [ "$(hash "$HG_CORE")" = "$HG_BEFORE" ] && [ "$(hg_attributes "$HG_CORE")" = "$HG_BEFORE_ATTRS" ] || return 1
        hg_native_switched=0
        printf 'HEVC_GATE_LIBRARY_ROLLBACK_RESTORED sha256=%s original_inode_already_in_place=1\n' "$HG_BEFORE"
        return 0
    fi
    [ ! -e "$HG_RESTORE" ] && [ ! -L "$HG_RESTORE" ] || return 1
    # Restore the original inode itself; a fresh copy cannot relink its old VMA.
    # HG_STAGED keeps the displaced new inode linked even if another process
    # loaded it between commit and this rollback. Both aliases remain retained.
    [ -f "$HG_STAGED" ] && [ ! -L "$HG_STAGED" ] && [ "$(hash "$HG_STAGED")" = "$HG_AFTER" ] || return 1
    ln "$HG_PREV" "$HG_RESTORE" && [ "$HG_PREV" -ef "$HG_RESTORE" ] && hg_atomic_replace "$HG_RESTORE" "$HG_CORE" && [ "$HG_CORE" -ef "$HG_PREV" ] && [ "$(hash "$HG_CORE")" = "$HG_BEFORE" ] && [ "$(hg_attributes "$HG_CORE")" = "$HG_BEFORE_ATTRS" ] || return 1
    hg_native_switched=0
    printf 'HEVC_GATE_LIBRARY_ROLLBACK_RESTORED sha256=%s original_inode_restored=1 retained_new_inode=1\n' "$HG_BEFORE"
}
