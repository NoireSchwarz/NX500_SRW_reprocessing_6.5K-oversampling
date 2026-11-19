#!/bin/sh
# R11f4 combined core activation. Not a bitrate/RTOS change.
set -eu
SYS=
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
umask 077
SD=$SYS/mnt/mmc
PKG=$SD/nx-lut-activation
OPS=$SD/nx-lut-demo/patch-fileops
H=$SYS/opt/usr/nxl-r11f4-activation
[ -d "$PKG" ] && [ ! -L "$PKG" ] && [ -f "$OPS" ] && [ ! -L "$OPS" ] || exit 70
[ "$("$OPS" sha256 "$OPS")" = c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d ] || exit 71
[ "$("$OPS" sha256 "$PKG/payload.sha256")" = 75524925afd766f04c7506e45b282850ee9850f2cd390259369ef2b84b65d91c ] || exit 72
"$OPS" version "$SYS/etc/version.info" || exit 73
[ -d "$SD/NXDEMO51" ] && [ ! -L "$SD/NXDEMO51" ] || exit 74
report=$SD/NXDEMO51/R11F4_ACTIVATION_$(date +%s)_$$
mkdir "$report"
exec > "$report/entry.log" 2>&1
printf 'NXL_R11F4_CORE_ACTIVATION1\nreport=%s\n' "$report"
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
fail(){ printf 'R11F4_ENTRY_FAILED=%s\n' "$1";if [ "${owned-0}" = 1 ];then phase_write FAILED || :;report_copy || :;notify FAILED;fi;exit 75; }
manifest_root=$PKG;manifest_file=$PKG/payload.sha256
check_manifest || fail PAYLOAD_IDENTITY_MISMATCH
for leaf in info.tg nx_cs.adj install.sh;do
 [ -f "$SD/$leaf" ] && [ ! -L "$SD/$leaf" ] && [ ! -e "$report/activation-$leaf" ] || fail ACTIVATION_ARCHIVE_COLLISION
 mv "$SD/$leaf" "$report/activation-$leaf" || fail ACTIVATION_ARCHIVE_FAILED
done
# Refuse source or module drift. This package activates exactly the installed
# R11f2 payload already evidenced on this camera; it never runs old builders.
D=$SYS/opt/usr/nx-lut-lite
[ -d "$D" ] && [ ! -L "$D" ] && [ "$("$OPS" sha256 "$D/state/module.manifest")" = 41dcfeb190077c2476568e082d861f74f5fc80b17a2e237e80c6a9e3e4709e26 ] || fail INSTALLED_MODULE_NOT_R11F2
manifest_root=$D;manifest_file=$D/state/module.manifest
check_manifest || fail INSTALLED_COMPONENT_MISMATCH
"$D/bin/nxl-video-probe.arm" --ready > "$report/camera-ready.txt" || fail CAMERA_BUSY_OR_USB_CONNECTED
"$D/bin/nxl-video-probe.arm" --status > "$report/counter-status.txt" || fail COUNTER_STATUS_UNAVAILABLE
# Pending or running samplers must be saved by the user first.
grep -q '^state=OFF$' "$report/counter-status.txt" || fail SAVE_COUNTERS_FIRST
[ ! -e "$H" ] && [ ! -L "$H" ] || fail EXISTING_ACTIVATION_PRESERVED
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
DISPLAY=:0 "$PKG/nxl-activation-ui" --working >/dev/null 2>&1 || :
"$PKG/nxl-gate-check.arm" --backup-snapshot "$report/snapshot-before.img" > "$report/snapshot-backup.txt" || fail SNAPSHOT_BACKUP_NOT_VERIFIED
# No internal writes occur before the full snapshot backup verifies.
mkdir "$H"
cp -R "$PKG/." "$H/"
mkdir "$H/state"
printf '%s\n' 75524925afd766f04c7506e45b282850ee9850f2cd390259369ef2b84b65d91c > "$H/state/manifest.sha256"
[ "$("$OPS" sha256 "$H/common.sh")" = 20ca435e5321313a277d9de3bee3b10d79a33ee15c8db806b458b76de527ab18 ] || fail INSTALLED_ACTIVATOR_COPY_MISMATCH
. "$H/common.sh"
owned=1
verify_payload || fail INSTALLED_ACTIVATOR_COPY_MISMATCH
printf '%s\n' "$report" > "$STATE/report.path"
cat "$SYS/proc/sys/kernel/random/boot_id" > "$STATE/request.boot"
phase_write PREPARED
for file in cmdline mounts uptime;do cat "$SYS/proc/$file" > "$report/before-$file.txt";done
pids=$(pidof di-camera-app 2>/dev/null) || pids=
set -- $pids
if [ "$#" = 1 ];then cat "$SYS/proc/$1/maps" > "$report/before-camera-maps.txt";fi
cp "$SYS/usr/lib/libmmfcore.so" "$report/visible-core-before.so"
# Install new, owned units only. Original camera/rescue/hibernate units stay.
U=$SYS/etc/systemd/system
W=$U/di-camera-app.service.wants
[ -d "$U" ] && [ ! -L "$U" ] && [ ! -L "$W" ] || fail SYSTEMD_PARENT_LINKED
for leaf in nxl-r11f4-preload.service nxl-r11f4-post.service;do
 [ ! -e "$U/$leaf" ] && [ ! -L "$U/$leaf" ] && [ ! -e "$W/$leaf" ] && [ ! -L "$W/$leaf" ] || fail SYSTEMD_PATH_COLLISION
 done
mode=$("$OPS" mount-info "$SYS/proc/mounts" "$SYS/usr/lib/libmmfcore.so")
[ "$mode" = "$ROOT ro" ] || fail ROOT_NOT_READ_ONLY
mount -o remount,rw "$ROOT" || fail ROOT_OPEN_FAILED
# Restore RO on every path, including a partial unit installation.
trap 'mount -o remount,ro "$ROOT" || :;sync' EXIT HUP INT TERM
[ -d "$W" ] || mkdir "$W"
for leaf in nxl-r11f4-preload.service nxl-r11f4-post.service;do
 cp "$H/$leaf" "$U/$leaf"
 [ "$(sha "$H/$leaf")" = "$(sha "$U/$leaf")" ] || fail SYSTEMD_COPY_MISMATCH
 ln -s "../$leaf" "$W/$leaf"
done
mount -o remount,ro "$ROOT" || fail ROOT_RESTORE_READONLY_FAILED
trap - EXIT HUP INT TERM
sync
"$SYS/usr/bin/systemctl" daemon-reload || fail SYSTEMD_RELOAD_FAILED
phase_write REFRESH_REQUESTED
# Archive our signed one-shot triggers before requesting recovery. No reboot loop.

report_copy || :
printf 'R11F4_REFRESH_REQUESTED=1; loading_not_yet_verified=1\n'
sync
"$SYS/usr/bin/systemctl" rescue || fail RECOVERY_TRANSITION_FAILED
