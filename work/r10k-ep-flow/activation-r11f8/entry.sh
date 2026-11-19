#!/bin/sh
# R11f8 combined core activation. Not a bitrate/RTOS change.
set -eu
SYS=
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
SD=$SYS/mnt/mmc
PKG=$SD/nx-lut-activation
OPS=$SD/nx-lut-demo/patch-fileops
H=$SYS/opt/usr/nxl-r11f8-activation
[ -d "$PKG" ] && [ ! -L "$PKG" ] && [ -f "$OPS" ] && [ ! -L "$OPS" ] || exit 70
[ "$("$OPS" sha256 "$OPS")" = c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d ] || exit 71
[ "$("$OPS" sha256 "$PKG/payload.sha256")" = 05e533bf4fd9a09a2b593b6fcc51849cc8f3eb9eda947f72763cbb3cd922d0ec ] || exit 72
"$OPS" version "$SYS/etc/version.info" || exit 73
[ -d "$SD/NXDEMO51" ] && [ ! -L "$SD/NXDEMO51" ] || exit 74
report=$SD/NXDEMO51/R11F8_ACTIVATION_$(date +%s)_$$
mkdir "$report"
exec > "$report/entry.log" 2>&1
printf 'NXL_R11F8_CORE_ACTIVATION1\nreport=%s\n' "$report"
check_manifest(){
 count=0
 while IFS=' ' read -r digest leaf extra;do
  [ -z "$extra" ] && [ "${#digest}" = 64 ] || return 1
  case "$digest" in *[!0-9a-f]*) return 1;;esac
  case "$leaf" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) return 1;;esac
  count=$((count+1));[ "$count" -le 256 ] || return 1
  oldifs=$IFS;IFS=/;set -- $leaf;IFS=$oldifs;component=$manifest_root
  for part in "$@";do component=$component/$part;[ ! -L "$component" ] || return 1;done
  [ -f "$component" ] && [ "$("$OPS" sha256 "$component")" = "$digest" ] || return 1
 done < "$manifest_file"
 [ "$count" -ge 10 ]
}
owned=0;ui_ok=0;root_open=0;handoff=0;old_retired=0;entry_stage=PAYLOAD_CHECK;entry_reason=UNEXPECTED_EXIT
entry_sync(){
 printf 'SYNC_BEGIN stage=%s\n' "$entry_stage"
 if sync;then sync_rc=0;else sync_rc=$?;fi
 printf 'SYNC_END stage=%s rc=%s\n' "$entry_stage" "$sync_rc"
 return "$sync_rc"
}
notice_run(){
 printf 'NOTICE_BEGIN flag=%s stage=%s\n' "$1" "$entry_stage"
 if DISPLAY=:0 "$PKG/nxl-activation-ui" "$1" >> "$report/notice.log" 2>&1;then notice_rc=0;else notice_rc=$?;fi
 printf 'NOTICE_END flag=%s rc=%s\n' "$1" "$notice_rc"
 # A display failure must neither hide the real cause nor enable activation.
 return 0
}
stage(){ entry_stage=$1;printf 'stage=%s time=%s\n' "$entry_stage" "$(date +%s)";printf '%s\n' "$entry_stage" > "$report/entry-stage.txt";entry_sync; }
entry_exit(){
 rc=$?;trap - EXIT HUP INT TERM;set +e
 if [ "$handoff" = 1 ];then
  printf 'ENTRY_HANDOFF_RELEASED rc=%s; pending_state_preserved=1\n' "$rc"
  exit "$rc"
 fi
 if [ "$root_open" = 1 ] && [ "$old_retired" = 1 ];then f6_restore_links || { rc=76;entry_reason=OLD_WANTS_RESTORE_FAILED; };fi
 if [ "$root_open" = 1 ];then mount -o remount,ro "${SYS:-/}";ro_rc=$?;sync;if [ "$ro_rc" != 0 ];then rc=76;entry_reason=ROOT_RESTORE_READONLY_FAILED;fi;fi
 if [ "$rc" != 0 ];then
  printf 'R11F8_ENTRY_FAILED=%s\nfailed_stage=%s\nexit_code=%s\n' "$entry_reason" "$entry_stage" "$rc"
  printf 'state=FAILED\nstage=%s\nreason=%s\nexit_code=%s\n' "$entry_stage" "$entry_reason" "$rc" > "$report/entry-result.txt"
  if [ "$owned" = 1 ];then phase_write FAILED;report_copy;fi
  if [ "$ui_ok" = 1 ];then
   flag=--failed;[ "$entry_stage" != SNAPSHOT_BACKUP ] || flag=--backup-failed
   notice_run "$flag"
  fi
  # Show the reason before the final global flush; record both boundaries.
  entry_sync
 fi
 printf 'ENTRY_EXIT_READY rc=%s\n' "$rc"
 exit "$rc"
}
trap entry_exit EXIT
trap 'entry_reason=INTERRUPTED;exit 74' HUP INT TERM
fail(){ entry_reason=$1;exit 75; }
manifest_root=$PKG;manifest_file=$PKG/payload.sha256
check_manifest || fail PAYLOAD_IDENTITY_MISMATCH
ui_ok=1
stage ARCHIVE_TRIGGERS
for leaf in info.tg nx_cs.adj install.sh;do
 [ -f "$SD/$leaf" ] && [ ! -L "$SD/$leaf" ] && [ ! -e "$report/activation-$leaf" ] || fail ACTIVATION_ARCHIVE_COLLISION
 mv "$SD/$leaf" "$report/activation-$leaf" || fail ACTIVATION_ARCHIVE_FAILED
done
# Refuse source or module drift. This package activates exactly the installed
# R11f2 payload already evidenced on this camera; it never runs old builders.
stage INSTALLED_MODULE_CHECK
D=$SYS/opt/usr/nx-lut-lite
[ -d "$D" ] && [ ! -L "$D" ] && [ "$("$OPS" sha256 "$D/state/module.manifest")" = 41dcfeb190077c2476568e082d861f74f5fc80b17a2e237e80c6a9e3e4709e26 ] || fail INSTALLED_MODULE_NOT_R11F2
manifest_root=$D;manifest_file=$D/state/module.manifest
check_manifest || fail INSTALLED_COMPONENT_MISMATCH
"$D/bin/nxl-video-probe.arm" --ready > "$report/camera-ready.txt" || fail CAMERA_BUSY_OR_USB_CONNECTED
"$D/bin/nxl-video-probe.arm" --status > "$report/counter-status.txt" || fail COUNTER_STATUS_UNAVAILABLE
# Pending or running samplers must be saved by the user first.
grep -q '^state=OFF$' "$report/counter-status.txt" || fail SAVE_COUNTERS_FIRST
[ ! -e "$H" ] && [ ! -L "$H" ] || fail EXISTING_ACTIVATION_PRESERVED
for previous in nxl-r11f4-activation nxl-r11f5-activation;do
 [ ! -e "$SYS/opt/usr/$previous" ] && [ ! -L "$SYS/opt/usr/$previous" ] || fail PREVIOUS_ACTIVATION_STATE_PRESERVED
done
stage OEM_AND_DEVICE_CHECK
[ -d "$SYS/opt/usr" ] && [ ! -L "$SYS/opt" ] && [ ! -L "$SYS/opt/usr" ] || fail INTERNAL_PARENT_LINKED
for parent in "$SYS/etc/systemd/system" "$SYS/run/systemd/system" "$SYS/usr/lib/systemd/system";do
 [ ! -e "$parent/di-camera-app.service.d" ] && [ ! -L "$parent/di-camera-app.service.d" ] || fail CAMERA_UNIT_OVERRIDE_PRESERVED
done
for parent in "$SYS/etc/systemd/system" "$SYS/run/systemd/system";do
 [ ! -e "$parent/di-camera-app.service" ] && [ ! -L "$parent/di-camera-app.service" ] || fail CAMERA_UNIT_OVERRIDE_PRESERVED
done
[ "$("$OPS" sha256 "$SYS/lib/ld-2.13.so")" = e740376dad45ec92d5c460e800f64f862bd850cb7783429179dc1a603036effd ] || fail OEM_LOADER_IDENTITY_MISMATCH
[ "$("$OPS" sha256 "$SYS/usr/bin/systemctl")" = 5ea6197e2d22f87cbf74f34eef289f7cabcfe0b96db92dcb45636ccefaa06d9f ] || fail OEM_TOOL_IDENTITY_MISMATCH
[ "$("$OPS" sha256 "$SYS/sbin/debugfs")" = 231b5e03d6380eac78ec77070c562d3ced3e7dba9e4a173913735cdad0227d60 ] || fail OEM_TOOL_IDENTITY_MISMATCH
[ "$("$OPS" sha256 "$SYS/usr/lib/libhibernation.so")" = fe10b00aea33234bb144c28f14892d4a0583a1b81d27f070a0e7e0f1b5198e03 ] || fail OEM_TOOL_IDENTITY_MISMATCH
[ "$("$OPS" sha256 "$SYS/usr/lib/systemd/system/di-camera-app.service")" = 5f7824afd875a7073618401d3e157af1f909530be12f1db7dee179c2a79d7f5c ] || fail OEM_TOOL_IDENTITY_MISMATCH
[ "$("$OPS" sha256 "$SYS/usr/lib/systemd/system/rescue.target")" = c57d0a3201fccc460d396e0f846c20f2def0fdbba32bf2a2d10bacd913a6c187 ] || fail OEM_TOOL_IDENTITY_MISMATCH
[ "$(cat "$SYS/sys/class/block/mmcblk0p10/dev")" = 179:10 ] && [ "$(cat "$SYS/sys/class/block/mmcblk0p9/dev")" = 179:9 ] || fail PARTITION_DEVICE_IDENTITY
[ "$(cat "$SYS/sys/class/block/mmcblk0p9/partition")" = 9 ] || fail SNAPSHOT_PARTITION_IDENTITY
root_mount=$(awk '$5=="/" {for(i=7;i<=NF;i++)if($i=="-"){print $3 " " $(i+2);break}}' "$SYS/proc/self/mountinfo")
[ "$root_mount" = '179:10 /dev/root' ] || fail ROOT_MOUNT_IDENTITY
stage RESUME_R11F6_CHECK
. "$PKG/resume-f6.sh"
f6_inspect || fail R11F6_STATE_OR_BACKUP_NOT_VERIFIED
stage RAW_CORE_READ
# debugfs defaults to read-only. Only stat and dump commands are permitted;
# never use -w, fsck, mkfs, image erasure or raw-block writes here.
"$SYS/sbin/debugfs" -R 'stat /usr/lib/libmmfcore.so' "$SYS/dev/mmcblk0p10" > "$report/raw-core.stat" 2>&1
"$SYS/sbin/debugfs" -R "dump /usr/lib/libmmfcore.so $report/raw-core.so" "$SYS/dev/mmcblk0p10" > "$report/raw-core-dump.log" 2>&1
raw=$("$OPS" sha256 "$report/raw-core.so") || fail RAW_CORE_DUMP_FAILED
case "$raw" in b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8|7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9) :;;*) fail UNKNOWN_PERSISTED_CORE_PRESERVED;;esac
printf 'RAW_CORE_SHA256=%s\n' "$raw"
# A complete backup remains on SD; do not replace it with a partial prefix.
available=$(df -Pk "$SD" | awk 'NR==2 {print $4}')
case "$available" in ''|*[!0-9]*) fail SD_SPACE_UNKNOWN;;esac
[ "$available" -ge 393216 ] || fail SD_BACKUP_SPACE_UNAVAILABLE
for item in /proc/partitions /proc/swaps /sys/class/block/mmcblk0p9/size /sys/class/block/mmcblk0p9/dev;do
 printf 'path=%s\n' "$item" >> "$report/backup-device-context.txt"
 cat "$SYS$item" >> "$report/backup-device-context.txt" 2>&1 || :
done
stage SNAPSHOT_BACKUP
notice_run --working
"$PKG/nxl-gate-check.arm" --backup-snapshot "$report/snapshot-before.img" > "$report/snapshot-backup.txt" || fail SNAPSHOT_BACKUP_NOT_VERIFIED
grep -q '^complete=1$' "$report/snapshot-backup.txt" || fail SNAPSHOT_BACKUP_NOT_COMPLETE
stage BACKUP_VERIFIED
notice_run --backup-ok
# No internal writes occur before the full snapshot backup verifies.
stage INSTALL_ACTIVATOR
mkdir "$H"
cp -R "$PKG/." "$H/"
mkdir "$H/state"
printf '%s\n' 05e533bf4fd9a09a2b593b6fcc51849cc8f3eb9eda947f72763cbb3cd922d0ec > "$H/state/manifest.sha256"
[ "$("$OPS" sha256 "$H/common.sh")" = 0613c0853ba4638429779728acac1e904f94aeaa2c5670fef5085d8f28568e3e ] || fail INSTALLED_ACTIVATOR_COPY_MISMATCH
. "$H/common.sh"
owned=1
verify_payload || fail INSTALLED_ACTIVATOR_COPY_MISMATCH
printf '%s\n' "$report" > "$STATE/report.path"
cat "$SYS/proc/sys/kernel/random/boot_id" > "$STATE/request.boot"
phase_write PREPARED
for file in cmdline mounts uptime;do cat "$SYS/proc/$file" > "$report/before-$file.txt";done
camera_find "$H/nxl-gate-check.arm" "$report/camera-process-before.txt" || fail CAMERA_PROCESS_DISCOVERY_FAILED
[ "$camera_state" = ONE ] || fail CAMERA_PROCESS_NOT_UNIQUE
cat "$SYS/proc/$camera_pid/maps" > "$report/before-camera-maps.txt"
cp "$report/resume-f6.txt" "$STATE/resume-f6.txt"
cp "$SYS/usr/lib/libmmfcore.so" "$report/visible-core-before.so"
stage INSTALL_BOOT_UNITS
# Install new, owned units only. Original camera/rescue/hibernate units stay.
U=$SYS/etc/systemd/system
W=$U/di-camera-app.service.wants
[ -d "$U" ] && [ ! -L "$U" ] && [ ! -L "$W" ] || fail SYSTEMD_PARENT_LINKED
for leaf in nxl-r11f8-preload.service nxl-r11f8-post.service;do
 [ ! -e "$U/$leaf" ] && [ ! -L "$U/$leaf" ] && [ ! -e "$W/$leaf" ] && [ ! -L "$W/$leaf" ] || fail SYSTEMD_PATH_COLLISION
 done
mode=$("$OPS" mount-info "$SYS/proc/mounts" "$SYS/usr/lib/libmmfcore.so")
[ "$mode" = "$ROOT ro" ] || fail ROOT_NOT_READ_ONLY
mount -o remount,rw "$ROOT" || fail ROOT_OPEN_FAILED
# Restore RO on every path, including a partial unit installation.
root_open=1
[ -d "$W" ] || mkdir "$W"
f6_retire || fail R11F6_UNIT_RETIRE_FAILED
cp "$report/resume-f6.txt" "$STATE/resume-f6.txt"
for leaf in nxl-r11f8-preload.service nxl-r11f8-post.service;do
 cp "$H/$leaf" "$U/$leaf"
 [ "$(sha "$H/$leaf")" = "$(sha "$U/$leaf")" ] || fail SYSTEMD_COPY_MISMATCH
 ln -s "../$leaf" "$W/$leaf"
done
mount -o remount,ro "$ROOT" || fail ROOT_RESTORE_READONLY_FAILED
root_open=0
sync
stage RELOAD_UNITS
"$SYS/usr/bin/systemctl" daemon-reload || fail SYSTEMD_RELOAD_FAILED
# Recheck after backup and unit preparation; never interrupt a new recording.
"$D/bin/nxl-video-probe.arm" --ready > "$report/handoff-ready.txt" || fail CAMERA_BECAME_BUSY
"$D/bin/nxl-video-probe.arm" --status > "$report/handoff-counter-status.txt" || fail COUNTER_STATUS_UNAVAILABLE
grep -q '^state=OFF$' "$report/handoff-counter-status.txt" || fail SAVE_COUNTERS_FIRST
phase_write REFRESH_REQUESTED
prepare_recovery || fail RECOVERY_PREPARATION_FAILED
report_copy || :
printf 'R11F8_REFRESH_REQUESTED=1; loading_not_yet_verified=1\n'
stage REQUEST_RECOVERY
handoff=1
cd "$ROOT" || exit 76
# exec releases the old SD script descriptor; all new execution/logs use RAM.
exec > "$REC/entry-release.log" 2>&1
exec /bin/sh "$REC/recovery.sh" launch
