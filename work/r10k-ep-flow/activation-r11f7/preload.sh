#!/bin/sh
# Runs before the first camera process on the refreshed boot. No watcher here.
set -eu
SYS=
H=$SYS/opt/usr/nxl-r11f7-activation
OPS=$H/patch-fileops
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d ] || exit 70
[ "$("$OPS" sha256 "$H/common.sh")" = ddb08bad342650b423acdc1943fbcf040e0e89addf6b5cd8dde5373d4b09e7b2 ] || exit 71
. "$H/common.sh"
cd "$ROOT"
exec >> "$STATE/preload.log" 2>&1
verify_payload || exit 72
phase=$(phase_read)
case "$phase" in FAILED) exit 0;;REFRESH_REQUESTED|VERIFIED) action=install;next=PRELOADED;;ROLLBACK_REQUESTED|ROLLED_BACK|PRELOAD_IN_PROGRESS|PRELOADED|ROLLBACK_PRELOADED) action=uninstall;next=ROLLBACK_PRELOADED;;*) exit 73;;esac
current=$(cat "$SYS/proc/sys/kernel/random/boot_id")
[ -n "$current" ] && [ "$current" != "$(cat "$STATE/request.boot")" ] || { printf 'FRESH_BOOT_NOT_OBSERVED\n';phase_write FAILED;report_copy || :;exit 0; }
# The post-check must execute from tmpfs: holding /opt/usr open can obstruct
# the original hibernation unmount/remount sequence.
RAM=$SYS/run/nxl-r11f7
[ ! -e "$RAM" ] && [ ! -L "$RAM" ] || { phase_write FAILED;report_copy || :;exit 0; }
mkdir "$RAM";chmod 700 "$RAM"
for leaf in common.sh post.sh nxl-gate-check.arm patch-fileops nxl-activation-ui;do
 cp "$H/$leaf" "$RAM/$leaf"
 [ "$(sha "$RAM/$leaf")" = "$(sha "$H/$leaf")" ] || exit 74
 chmod 700 "$RAM/$leaf"
done
printf 'FAILED\n' > "$RAM/expected-phase"
# Never install into an already running camera process.
camera_find "$H/nxl-gate-check.arm" "$STATE/preload-process.txt" || { printf 'PROCESS_DISCOVERY_UNAVAILABLE\n';phase_write FAILED;report_copy || :;exit 0; }
[ "$camera_state" = NONE ] || { printf 'CAMERA_STARTED_BEFORE_PRELOAD\n';phase_write FAILED;report_copy || :;exit 0; }
printf '%s\n' "$current" > "$STATE/current.boot"
phase_write PRELOAD_IN_PROGRESS
printf '%s\n' "$next" > "$RAM/expected-phase"
if /bin/sh "$H/commit.sh" "$action";then
 # Exercise the actual OEM loader and kernel mappings before the camera can
 # load the new library. --verify/--list do not run camera constructors.
 loader=$SYS/lib/ld-2.13.so
 loader_ok=0
 if [ "$(sha "$loader")" = e740376dad45ec92d5c460e800f64f862bd850cb7783429179dc1a603036effd ];then
  if LD_LIBRARY_PATH=/usr/lib:/usr/lib/driver:/lib "$loader" --verify "$SYS/usr/apps/com.samsung.di-camera-app/bin/di-camera-app" > "$STATE/loader-verify.txt" 2>&1 &&
     LD_LIBRARY_PATH=/usr/lib:/usr/lib/driver:/lib "$loader" --list "$SYS/usr/apps/com.samsung.di-camera-app/bin/di-camera-app" > "$STATE/loader-list.txt" 2>&1;then loader_ok=1;fi
 fi
 if [ "$loader_ok" = 1 ];then phase_write "$next"
 elif [ "$action" = install ] && /bin/sh "$H/commit.sh" uninstall;then
  printf 'NATIVE_LOADER_PREFLIGHT_FAILED_OEM_RESTORED\n'
  phase_write ROLLBACK_PRELOADED
  printf 'ROLLBACK_PRELOADED\n' > "$RAM/expected-phase"
 else
  phase_write FAILED
  printf 'FAILED\n' > "$RAM/expected-phase"
 fi
else printf 'PRELOAD_COMMIT_FAILED\n';phase_write FAILED;printf 'FAILED\n' > "$RAM/expected-phase";fi
exit 0
