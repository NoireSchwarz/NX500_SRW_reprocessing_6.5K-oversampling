#!/bin/sh
# R11f9 live adoption. Never requests rescue/reboot or changes the core library.
set -eu
SYS=
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
SD=$SYS/mnt/mmc
PKG=$SD/nx-lut-activation
OPS=$PKG/patch-fileops
D=$SYS/opt/usr/nx-lut-lite
H=$SYS/opt/usr/nxl-r11f9-activation
ROOT=${SYS:-/}
MANIFEST=6ca1f5f8d44eeb780baea6cf4c32f851697bde07c04f539e97b67cbc18350d0a
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d ] || exit 70
[ "$("$OPS" sha256 "$PKG/payload.sha256")" = "$MANIFEST" ] || exit 71
[ -d "$SD/NXDEMO51" ] && [ ! -L "$SD/NXDEMO51" ] || exit 72
report=$SD/NXDEMO51/R11F9_ACTIVATION_$(date +%s)_$$
mkdir "$report"
exec > "$report/entry.log" 2>&1
printf 'NXL_R11F9_LIVE_ADOPTION1\nreport=%s\nautomatic_recovery=DISABLED\n' "$report"
# Source only the exact payload versions bound by this entry.
[ "$("$OPS" sha256 "$PKG/checks.sh")" = dab8b3cf724cac894a8e16977784e74d70b6f59ab62a99d818e188e5f038be1c ] || exit 73
[ "$("$OPS" sha256 "$PKG/resume-f8.sh")" = 295e7b27c50fdfbcdf268beae815c997c2ce962498a67b62482df00039ee4ee8 ] || exit 73
. "$PKG/checks.sh"
. "$PKG/resume-f8.sh"
reason=UNEXPECTED_EXIT;stage_name=PAYLOAD;final_state=PENDING;ui_ok=0;root_open=0;finished=0;trigger_archiving=0
LOCK=$SYS/run/nxl-r11f9-entry.lock
stage(){ stage_name=$1;printf 'stage=%s time=%s\n' "$1" "$(date +%s)";printf '%s\n' "$1" > "$report/entry-stage.txt"; }
notice(){
 ( code=0;DISPLAY=:0 "$PKG/nxl-activation-ui" "$1" >> "$report/notice.log" 2>&1 || code=$?
 printf 'notice=%s return_code=%s\n' "$1" "$code" >> "$report/entry.log"
 return 0 )
}
finish(){
 code=$?;trap - EXIT HUP INT TERM;set +e
 if [ "$root_open" = 1 ];then
  f8_restore || { reason=OLD_LINK_RESTORE_ERROR;final_state=ERROR; }
  mount -o remount,ro "$ROOT" || { reason=ROOT_READONLY_RESTORE_ERROR;final_state=ERROR; }
 fi
 if [ "$finished" != 1 ];then
  if [ "$trigger_archiving" = 1 ];then
   # A partial archive must not remove the next attempt's entry points.
   for leaf in info.tg nx_cs.adj install.sh;do
    if regular "$report/activation-$leaf" && absent "$SD/$leaf";then
     mv "$report/activation-$leaf" "$SD/$leaf" || { reason=TRIGGER_RESTORE_ERROR;final_state=ERROR; }
    elif ! regular "$SD/$leaf";then reason=TRIGGER_RESTORE_ERROR;final_state=ERROR;fi
   done
  fi
  retry=1;[ "$final_state" != ERROR ] || retry=0
  printf '%s\n' "$final_state" > "$report/phase"
  printf 'state=%s\nreason=%s\nstage=%s\nexit_code=%s\nretry_allowed=%s\nrecovery_requested=0\ncore_write_performed=0\nend=NXL_R11F9_RESULT1\n' "$final_state" "$reason" "$stage_name" "$code" "$retry" > "$report/result.txt"
  if [ "$ui_ok" = 1 ];then notice --pending;fi
 fi
 sync
 exit "$code"
}
trap finish EXIT
trap 'reason=INTERRUPTED_RETRY_ALLOWED;exit 74' HUP INT TERM
pending(){ reason=$1;exit 0; }
manifest "$PKG" "$PKG/payload.sha256" 32 || pending PAYLOAD_IDENTITY_MISMATCH
ui_ok=1
for leaf in install.sh nx_cs.adj info.tg;do
 regular "$SD/$leaf" || pending INSTALL_TRIGGER_MISSING
 cp "$SD/$leaf" "$report/attempt-$leaf" || pending INSTALL_TRIGGER_ARCHIVE_ERROR
done
pin "$SYS/usr/bin/flock" dbf2c45194ce4d6a66978022db379dbe490b70ed82877f21c32bd32474735322 || pending LOCK_TOOL_IDENTITY_MISMATCH
[ ! -L "$LOCK" ] && { absent "$LOCK" || regular "$LOCK"; } || pending LOCK_PATH_UNSAFE
# Kernel releases the advisory lock on every exit, including SIGKILL.
exec 9>> "$LOCK"
"$SYS/usr/bin/flock" -n 9 || pending ANOTHER_CHECK_RUNNING
notice --working
stage INSTALLED_MODULE_CHECK
"$OPS" version "$SYS/etc/version.info" || pending FIRMWARE_NOT_SUPPORTED
pin "$D/state/module.manifest" 41dcfeb190077c2476568e082d861f74f5fc80b17a2e237e80c6a9e3e4709e26 && manifest "$D" "$D/state/module.manifest" 128 || pending INSTALLED_MODULE_IDENTITY_MISMATCH
pin "$SYS/usr/bin/systemctl" 5ea6197e2d22f87cbf74f34eef289f7cabcfe0b96db92dcb45636ccefaa06d9f || pending SYSTEM_MANAGER_IDENTITY_MISMATCH
stage LIVE_READ_ONLY_CHECK
stable_live initial || pending "$reason"
initial_token=$stable_token
stage PRESERVE_F8_AND_COMPLETE_BACKUPS
f8_inspect || pending "$reason"
stage FINAL_LIVE_READ_ONLY_CHECK
stable_live final || pending "$reason"
[ "$stable_token" = "$initial_token" ] || pending PROCESS_CHANGED_DURING_BACKUP_CHECK
stage SAVE_ADOPTION_RECEIPT
# Only a new receipt/payload directory is created. No library is replaced.
directory "$SYS/opt/usr" && [ ! -L "$SYS/opt" ] && [ ! -L "$H" ] || pending INTERNAL_PATH_UNSAFE
if absent "$H";then mkdir "$H" || pending RECEIPT_DIRECTORY_CREATE_ERROR;else directory "$H" || pending INTERNAL_PATH_UNSAFE;fi
# Existing immutable receipt payload may be completed, never overwritten.
if absent "$H/payload.sha256";then cp "$PKG/payload.sha256" "$H/payload.sha256" || pending RECEIPT_COPY_ERROR;fi
pin "$H/payload.sha256" "$MANIFEST" || pending RECEIPT_MANIFEST_MISMATCH
while IFS=' ' read -r digest leaf extra;do
 case "$leaf" in */*|*[!A-Za-z0-9_.-]*|'') pending RECEIPT_LEAF_UNSAFE;;esac
 regular "$PKG/$leaf" || pending RECEIPT_SOURCE_TYPE_INVALID
 if absent "$H/$leaf";then cp "$PKG/$leaf" "$H/$leaf" || pending RECEIPT_COPY_ERROR;fi
 pin "$H/$leaf" "$digest" || pending RECEIPT_EXISTING_CONTENT_MISMATCH
done < "$PKG/payload.sha256"
manifest "$H" "$H/payload.sha256" 32 || pending RECEIPT_PAYLOAD_INVALID
stage RETIRE_OWNED_F8_START_LINKS
U=$SYS/etc/systemd/system;W=$U/di-camera-app.service.wants
for parent in "$SYS/etc" "$SYS/etc/systemd" "$U" "$W";do directory "$parent" || pending SYSTEMD_PARENT_UNSAFE;done
mode=$("$OPS" mount-info "$SYS/proc/mounts" "$SYS/usr/lib/libmmfcore.so") || pending ROOT_MODE_UNAVAILABLE
[ "$mode" = "$ROOT ro" ] || pending ROOT_NOT_READONLY
mount -o remount,rw "$ROOT" || pending ROOT_OPEN_ERROR
root_open=1
f8_retire || pending OLD_LINK_RETIRE_ERROR
mount -o remount,ro "$ROOT" || pending ROOT_READONLY_RESTORE_ERROR
root_open=0
run MANAGER_RELOAD "$report/reload.txt" "$SYS/usr/bin/systemctl" daemon-reload || pending MANAGER_RELOAD_ERROR
# Reload does not stop/start the camera. Prove the same process is still valid.
idle published && live published || pending "$reason"
[ "$token" = "$initial_token" ] || pending PROCESS_CHANGED_BEFORE_PUBLICATION
stage ARCHIVE_VERIFIED_TRIGGERS
trigger_archiving=1
for leaf in info.tg nx_cs.adj install.sh;do
 regular "$SD/$leaf" && absent "$report/activation-$leaf" || pending TRIGGER_ARCHIVE_COLLISION
 mv "$SD/$leaf" "$report/activation-$leaf" || pending TRIGGER_ARCHIVE_ERROR
done
stage VERIFIED
printf 'state=VERIFIED\nreason=EXISTING_LIVE_CORE_ADOPTED\npid=%s\nticks=%s\nboot_id=%s\nrecovery_requested=0\ncore_write_performed=0\nold_state_preserved=1\nend=NXL_R11F9_RESULT1\n' "$camera_pid" "$camera_ticks" "$current_boot" > "$report/result.txt"
printf 'VERIFIED\n' > "$report/phase"
# One append-only receipt per successful attempt; earlier results are retained.
receipt=$H/receipt-$(date +%s)-$$.txt
absent "$receipt" && absent "$receipt.pending" || pending RECEIPT_PUBLISH_COLLISION
cp "$report/result.txt" "$receipt.pending" && mv "$receipt.pending" "$receipt" || pending RECEIPT_PUBLISH_ERROR
sync
finished=1
notice --active
exit 0
