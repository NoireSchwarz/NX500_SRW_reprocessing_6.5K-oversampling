#!/bin/sh
# Loop, executable and open script fd stay in /run. A final, bounded state
# transaction pins /opt/usr with cwd, then releases it before notification
# or rescue. OEM hib_emmc_part_preproc retries EBUSY rather than unmounting
# underneath that short transaction.
set -u
SYS=
RAM=$SYS/run/nxl-r11f6
H=$SYS/opt/usr/nxl-r11f6-activation
. "$RAM/common.sh"
cd "$ROOT"
exec > "$RAM/post.log" 2>&1
phase=$(cat "$RAM/expected-phase") || exit 70
case "$phase" in FAILED) notify FAILED;exit 0;;PRELOADED) mode=--check;terminal=VERIFIED;;ROLLBACK_PRELOADED) mode=--check-oem;terminal=ROLLED_BACK;;*) exit 71;;esac
finish_state(){
 (
  cd "$STATE" || exit 1
  actual=$(phase_read) || exit 1
  case "$actual" in VERIFIED|ROLLED_BACK|FAILED) exit 0;;esac
  [ "$actual" = "$phase" ] || exit 1
  cp "$RAM/post-check.txt" "$STATE/post-check.txt" || exit 1
  phase_write "$terminal" || exit 1
  cp "$RAM/post.log" "$STATE/post.log" || :
  report_copy || :
  : > "$RAM/newly-finalized"
 )
}
rounds=0;good=0
while [ "$rounds" -lt 180 ];do
 rounds=$((rounds+1))
 pids=$(pidof di-camera-app 2>/dev/null) || pids=
 set -- $pids
 if [ "$#" = 1 ];then
  pid=$1;case "$pid" in ''|*[!0-9]*) pid=;;esac
  if [ -n "$pid" ] && "$RAM/nxl-gate-check.arm" "$mode" "$pid" > "$RAM/post-check.txt";then
   if [ -f "$RAM/post-check.previous" ] && "$RAM/patch-fileops" same "$RAM/post-check.previous" "$RAM/post-check.txt";then good=$((good+1));else good=1;fi
   cp "$RAM/post-check.txt" "$RAM/post-check.previous"
  else good=0;fi
 else
  good=0
  if "$SYS/usr/bin/systemctl" show -p ActiveState di-camera-app.service > "$RAM/camera-service.txt" 2>&1 && grep -q '^ActiveState=failed$' "$RAM/camera-service.txt";then
   printf 'EARLY_CAMERA_START_FAILED\n'
   break
  fi
 fi
 if [ "$rounds" -ge 40 ] && [ "$good" -ge 10 ];then
  if finish_state;then
   [ ! -e "$RAM/newly-finalized" ] || notify "$terminal"
   exit 0
  fi
 fi
 sleep 1
done
# Camera absent after loader failure is distinct from a user recording.
safe=0
probe=$SYS/opt/usr/nx-lut-lite/bin/nxl-video-probe.arm
# Never replace or restart beneath unsaved/active collector state.
if [ -x "$probe" ] && [ "$("$RAM/patch-fileops" sha256 "$probe")" = 7228073bea11ef7eb9fa050075887c390643674f0021eb1254ac77ec7eacf7e7 ];then
 if "$probe" --status > "$RAM/rollback-counter-status.txt" && grep -q '^state=OFF$' "$RAM/rollback-counter-status.txt";then
  if "$probe" --ready > "$RAM/rollback-ready.txt";then safe=1
  elif grep -q '^reason=TRACE_CAMERA_UNAVAILABLE$' "$RAM/rollback-ready.txt";then
   pids=$(pidof di-camera-app 2>/dev/null) || pids=
   [ -n "$pids" ] || safe=1
  fi
 fi
fi
(
 cd "$STATE" || exit 1
 actual=$(phase_read) || exit 1
 case "$actual" in VERIFIED|ROLLED_BACK|FAILED) exit 0;;esac
 cp "$RAM/post-check.txt" "$STATE/post-check.txt" 2>/dev/null || :
 cp "$RAM/post.log" "$STATE/post.log" || :
 for leaf in rollback-ready.txt rollback-counter-status.txt camera-service.txt;do
  [ ! -f "$RAM/$leaf" ] || cp "$RAM/$leaf" "$STATE/$leaf" || :
 done
 if [ "$phase" = PRELOADED ] && [ "$actual" = PRELOADED ] && [ "$safe" = 1 ];then
  if /bin/sh "$H/commit.sh" uninstall;then
   cat "$SYS/proc/sys/kernel/random/boot_id" > "$STATE/request.boot"
   phase_write ROLLBACK_REQUESTED || exit 1
   report_copy || :
   : > "$RAM/request-rollback"
   exit 0
  fi
 fi
 phase_write FAILED || :
 report_copy || :
) || :
# No cwd or script fd pins /opt/usr when the recovery transition starts.
if [ -f "$RAM/request-rollback" ];then
 "$SYS/usr/bin/systemctl" rescue
 exit 0
fi
notify FAILED
exit 1

