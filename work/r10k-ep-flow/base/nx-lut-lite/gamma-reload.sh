#!/bin/sh
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
GAMMA=$D/bin/nxl-gamma-reload.arm
USB=$D/bin/nxl-usb-guard.arm
ENGINE=$D/lut.sh
REFRESH=$D/bin/nxl-native-refresh.arm
NATIVE_REFRESH_SHA=3a7b272e3be9ad81996c823f17f33d84e990a85eff0f252785db7d575a0ceac7
IDLE=$D/bin/nxl-lutidle.arm
LIFE=$D/bin/nxl-gamma-life.arm
GAMMA_LIFE_SHA=b7d63b63abe6a2261da9f0f3ff723e6cd7a095e110cadc28c4bab95bd8766adb
LUT_IDLE_SHA=da2ba4416c36dbf1fa823edf70d019ef79f838df6e48da427ea5ed708ce327b7
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
GAMMA_RELOAD_SHA=eaa49f88916c051ef6e01fad4744ae55eab68b85112ed01ea7a26059ee3ffae8
USB_GUARD_SHA=61bf59b3bf4ccba775525e40024d0a42b86cffd22a1b94d9a431a8102f266918
ENGINE_SHA=35ee376929a40f76e884b617d6d19b0fac63002966f29a065d045bdb4c2590cd
gamma_run_log=
gamma_timing(){
 [ -n "$gamma_run_log" ] || return 0
 if IFS=' ' read -r boot_seconds rest < /proc/uptime;then
  printf 'phase_%s_uptime=%s\n' "$1" "$boot_seconds" >> "$gamma_run_log" || :
 fi
}
fail(){
 [ -z "$gamma_run_log" ] || printf 'reason=%s\n' "$1" 2>/dev/null >> "$gamma_run_log" || :
 printf 'NXL_GAMMA_RELOAD1\nmode=FACTORY\nphase=FAILED\nreason=%s\nepoch=0\nqueued_epoch=0\nqualified=0\napplies=0\ninvalidations=0\nqueues=0\nfault=0\nend=NXL_GAMMA_RELOAD_END1\n' "$1"
 exit 1
}
case "${1:-}" in status|lift|log|restore|toggle) :;;*) exit 2;;esac
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] || fail GAMMA_COMPONENT_IDENTITY_FAILED
[ -f "$GAMMA" ] && [ ! -L "$GAMMA" ] && [ -x "$GAMMA" ] && [ "$("$OPS" sha256 "$GAMMA")" = "$GAMMA_RELOAD_SHA" ] || fail GAMMA_COMPONENT_IDENTITY_FAILED
[ -f "$USB" ] && [ ! -L "$USB" ] && [ -x "$USB" ] && [ "$("$OPS" sha256 "$USB")" = "$USB_GUARD_SHA" ] || fail USB_COMPONENT_IDENTITY_FAILED
. "$D/process.sh"
nxl_require_normal_mode || fail GAMMA_FACTORY_SCRIPT_MODE
[ "$1" != status ] || { [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || fail LUT_IDLE_IDENTITY_FAILED;exec "$IDLE" --gamma-state; }
"$USB" preflight BEFORE_GAMMA >/dev/null 2>&1 || fail USB_CONNECTED_OR_UNAVAILABLE
RUN=/tmp/nx-lut-lite
mkdir -p "$RUN" || fail GAMMA_LOCK_DIRECTORY_FAILED
lock=$RUN/clean-log-command.lock
if ! mkdir "$lock" 2>/dev/null;then
    [ -r "$lock/pid" ] || fail GAMMA_COMMAND_BUSY
    IFS= read -r prior < "$lock/pid" || fail GAMMA_COMMAND_BUSY
    case "$prior" in ''|*[!0-9]*) fail GAMMA_COMMAND_BUSY;;esac
    kill -0 "$prior" 2>/dev/null && fail GAMMA_COMMAND_BUSY
    rm -f "$lock/pid";rmdir "$lock" 2>/dev/null || fail GAMMA_COMMAND_BUSY
    mkdir "$lock" 2>/dev/null || fail GAMMA_COMMAND_BUSY
fi
printf '%s\n' "$$" > "$lock/pid" || fail GAMMA_LOCK_SAVE_FAILED
cleanup(){
 gamma_exit=$?
 gamma_timing finished
 [ -z "$gamma_run_log" ] || printf 'exit=%s\n' "$gamma_exit" 2>/dev/null >> "$gamma_run_log" || :
 rm -f "$lock/pid"
 rmdir "$lock" 2>/dev/null || :
}
trap cleanup 0
trap 'exit 130' HUP INT TERM
engine_ok(){ [ -f "$ENGINE" ] && [ ! -L "$ENGINE" ] && [ "$("$OPS" sha256 "$ENGINE")" = "$ENGINE_SHA" ]; }
gamma_start_run_log(){
 log_parent=/mnt/mmc/NXDEMO51/logs
 log_root=$log_parent/vlog-switch
 [ -d "$log_parent" ] && [ ! -L "$log_parent" ] && [ ! -L /mnt/mmc/NXDEMO51 ] || return 1
 if ! mkdir "$log_root" 2>/dev/null;then
  [ -d "$log_root" ] && [ ! -L "$log_root" ] || return 1
 fi
 for log_slot in "$log_root/run-1" "$log_root/run-2" "$log_root/run-3";do
  [ ! -L "$log_slot" ] && { [ ! -e "$log_slot" ] || [ -d "$log_slot" ]; } || return 1
  if [ -d "$log_slot" ];then
   if [ -e "$log_slot/request.txt" ];then
    [ ! -L "$log_slot/request.txt" ] && [ -f "$log_slot/request.txt" ] &&
     [ "$(sed -n '1p' "$log_slot/request.txt")" = NXL_VLOG_RUN1 ] || return 1
   else
    # An interrupted mkdir may have left an empty slot.
    rmdir "$log_slot" || return 1
   fi
  fi
 done
 # The command lock serializes rotation and all native reports for this run.
 log_next=1
 for log_number in 1 2 3;do
  [ -d "$log_root/run-$log_number" ] || continue
  [ "$log_next" = "$log_number" ] ||
   mv "$log_root/run-$log_number" "$log_root/run-$log_next" || return 1
  log_next=$((log_next+1))
 done
 rm -rf -- "$log_root/run-3" || return 1
 [ ! -d "$log_root/run-2" ] || mv "$log_root/run-2" "$log_root/run-3" || return 1
 [ ! -d "$log_root/run-1" ] || mv "$log_root/run-1" "$log_root/run-2" || return 1
 NXL_GAMMA_AUDIT_DIR=$log_root/run-1
 mkdir "$NXL_GAMMA_AUDIT_DIR" || return 1
 printf 'NXL_VLOG_RUN1\nrequest=%s\n' "$1" > "$NXL_GAMMA_AUDIT_DIR/request.txt" || return 1
 gamma_run_log=$NXL_GAMMA_AUDIT_DIR/request.txt
 gamma_timing start
 export NXL_GAMMA_AUDIT_DIR
}
gamma_restore_request(){
 restore_command=$1
 restore_result=$2
 for retry_delay in 0.17 0.43 0.71 last;do
  if "$GAMMA" "$restore_command" > "$restore_result";then return 0
  else restore_rc=$?;fi
  # The host reports this reason only after RTOS retirement and cleanup.
  [ "$restore_rc" = 1 ] && [ "$retry_delay" != last ] &&
   [ "$(sed -n '1p' "$restore_result")" = NXL_GAMMA_RELOAD1 ] &&
   [ "$(sed -n 's/^reason=//p' "$restore_result")" = GAMMA_NATIVE_BUSY_RETRY ] &&
   grep -qx 'fault=0' "$restore_result" &&
   [ "$(tail -n 1 "$restore_result")" = end=NXL_GAMMA_RELOAD_END1 ] || return "$restore_rc"
  sleep "$retry_delay" || return "$restore_rc"
  "$USB" preflight BEFORE_GAMMA >/dev/null 2>&1 || fail USB_CONNECTED_OR_UNAVAILABLE
 done
}
gamma_wait(){
 wanted=$1;phase=$2;shift 2;file=$RUN/gamma-confirm.txt
 gamma_refresh_pending=0
 [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || return 1
 for interval in "$@";do
  gamma_refresh_pending=0
  if "$IDLE" --gamma-state > "$file" 2>&1 &&
   grep -qx 'NXL_GAMMA_RELOAD1' "$file" && grep -qx 'end=NXL_GAMMA_RELOAD_END1' "$file" &&
   grep -qx "mode=$wanted" "$file" && grep -qx 'fault=0' "$file";then
   e=$(sed -n 's/^epoch=//p' "$file");q=$(sed -n 's/^queued_epoch=//p' "$file")
   case "$e" in ''|*[!0-9]*) return 1;;esac
   case "$q" in ''|*[!0-9]*) return 1;;esac
   if [ "$e" = "$q" ] && { grep -qx "phase=$phase" "$file" || { [ "$wanted" = FACTORY ] && grep -qx 'phase=OFF' "$file"; }; };then
    return 0
   fi
   if [ "$e" != 0 ] && [ "$e" != "$q" ] && grep -qx 'phase=WAIT_NATIVE' "$file" &&
    grep -qx 'reason=GAMMA_WAIT_NATIVE_UPDATE' "$file";then
    gamma_refresh_pending=1
   fi
  fi
  [ "$interval" = 0 ] || sleep "$interval"
 done
 return 1
}
# The verified native helper may already have returned the queued epoch. The LUT
# engine still reads live Gamma independently before touching the table.
gamma_reply_queued(){
 LC_ALL=C awk -v mode="$1" -v phase="$2" '
  NR==1 { header=($0=="NXL_GAMMA_RELOAD1") }
  /^mode=/ { m++; md=substr($0,6) }
  /^phase=/ { p++; ph=substr($0,7) }
  /^epoch=/ { e++; ep=substr($0,7) }
  /^queued_epoch=/ { q++; qe=substr($0,14) }
  /^fault=/ { f++; fault=substr($0,7) }
  END { exit !(header && $0=="end=NXL_GAMMA_RELOAD_END1" &&
          m==1 && md==mode && p==1 && (ph==phase || (mode=="FACTORY" && ph=="OFF")) &&
          e==1 && q==1 && ep ~ /^[0-9]+$/ && qe ~ /^[0-9]+$/ && (ep "x")== (qe "x") &&
          f==1 && fault=="0") }' "$3"
}
gamma_queue_confirm(){
 wanted=$1;phase=$2
 if [ "$#" -eq 3 ] && gamma_reply_queued "$wanted" "$phase" "$3";then
  cp "$3" "$RUN/gamma-confirm.txt" || fail GAMMA_QUEUE_RECEIPT_SAVE_FAILED
  gamma_timing queued_native_reply
 elif ! gamma_wait "$wanted" "$phase" 0.10 0.25 0.40 0;then
  [ "$gamma_refresh_pending" = 1 ] || fail GAMMA_NATIVE_QUEUE_NOT_CONFIRMED
  [ -f "$REFRESH" ] && [ ! -L "$REFRESH" ] && [ -x "$REFRESH" ] && [ "$("$OPS" sha256 "$REFRESH")" = "$NATIVE_REFRESH_SHA" ] || fail NATIVE_REFRESH_IDENTITY_FAILED
  "$USB" preflight BEFORE_GAMMA >/dev/null 2>&1 || fail USB_CONNECTED_OR_UNAVAILABLE
  gamma_timing native_refresh
  "$REFRESH" photo > "$RUN/gamma-native-refresh.log" 2>&1 || fail GAMMA_NATIVE_REFRESH_UNCONFIRMED
  gamma_wait "$wanted" "$phase" 0.10 0.25 0.40 0.65 1 1 1 1 1 1 0 || fail GAMMA_NATIVE_QUEUE_NOT_CONFIRMED
 fi
 gamma_timing queue_confirmed
}
gamma_confirm(){
 gamma_queue_confirm "$@"
 # Keep the Gamma response as the final record, for the existing strict parser.
 if ! /bin/sh "$ENGINE" reload > "$RUN/gamma-lut-reload.log" 2>&1;then
  printf 'NXL_GAMMA_CONFIGURED1\n'
  cat "$RUN/gamma-confirm.txt"
  if grep -qx 'ERR LUT_RELOAD_RESTART_REQUIRED' "$RUN/gamma-lut-reload.log";then
   printf 'ERR LUT_RELOAD_RESTART_REQUIRED\n'
  fi
  fail GAMMA_LUT_RELOAD_UNCONFIRMED
 fi
 cat "$RUN/gamma-confirm.txt"
}
# Reconcile only on an explicit user control request, never on status/startup.
# A pending retirement must finish a native factory transaction and LUT cleanup
# before any subsequent Log request. Preserve all failures for the next retry.
[ -f "$LIFE" ] && [ ! -L "$LIFE" ] && [ -x "$LIFE" ] && [ "$("$OPS" sha256 "$LIFE")" = "$GAMMA_LIFE_SHA" ] || fail GAMMA_LIFECYCLE_IDENTITY_FAILED
gamma_start_run_log "$1" || fail GAMMA_RUN_LOG_UNAVAILABLE
"$LIFE" reconcile > "$RUN/gamma-life-reconcile.txt" 2>&1 || fail GAMMA_LIFECYCLE_RECONCILE_BLOCKED
if grep -qx 'outcome=RESTORE_FRESH_REQUIRED' "$RUN/gamma-life-reconcile.txt" || grep -qx 'outcome=RESTORE_REQUIRED' "$RUN/gamma-life-reconcile.txt";then
 if grep -qx 'outcome=RESTORE_FRESH_REQUIRED' "$RUN/gamma-life-reconcile.txt";then recover=restore-fresh;else recover=restore;fi
 gamma_restore_request "$recover" "$RUN/gamma-life-factory-request.txt" || { cat "$RUN/gamma-life-factory-request.txt";exit 1; }
 gamma_queue_confirm FACTORY FACTORY_QUEUED
 engine_ok || fail GAMMA_LIFECYCLE_FACTORY_RESTORED_LOOK_CLEANUP_REQUIRED
 NXL_LUT_DEFER_RELOAD=0 /bin/sh "$ENGINE" clean-log-release > "$RUN/gamma-life-look-cleanup.txt" 2>&1 || fail GAMMA_LIFECYCLE_LOOK_CLEANUP_UNCONFIRMED
 gamma_queue_confirm FACTORY FACTORY_QUEUED
 cat "$RUN/gamma-confirm.txt" > "$RUN/gamma-life-confirm.txt"
 "$LIFE" complete > "$RUN/gamma-life-complete.txt" 2>&1 || fail GAMMA_LIFECYCLE_FACTORY_VERIFICATION_FAILED
 # A toggle from an unknown stale state ends safely at Factory. A second
 # deliberate toggle enables Log; explicit 'log' may continue immediately.
 if [ "$1" != log ];then cat "$RUN/gamma-life-confirm.txt";exit 0;fi
fi
if [ "$1" = toggle ];then
 if [ -e "$RUN/clean-log.policy" ] || [ -L "$RUN/clean-log.policy" ];then set -- restore
 else
  [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || fail LUT_IDLE_IDENTITY_FAILED
  "$IDLE" --gamma-state > "$RUN/gamma-toggle-state.txt" 2>&1 || fail GAMMA_TOGGLE_STATE_UNAVAILABLE
  grep -qx 'NXL_GAMMA_RELOAD1' "$RUN/gamma-toggle-state.txt" && grep -qx 'end=NXL_GAMMA_RELOAD_END1' "$RUN/gamma-toggle-state.txt" || fail GAMMA_TOGGLE_STATE_INVALID
  if grep -qx 'mode=FACTORY' "$RUN/gamma-toggle-state.txt" && grep -Eq '^phase=(OFF|FACTORY_QUEUED)$' "$RUN/gamma-toggle-state.txt" && grep -qx 'fault=0' "$RUN/gamma-toggle-state.txt";then set -- log
  else set -- restore;fi
 fi
fi
case "$1" in
log)
    engine_ok || fail CLEAN_LOG_ENGINE_IDENTITY_FAILED
    NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-prepare > "$RUN/clean-log-prepare.log" 2>&1 || fail CLEAN_LOG_PREPARE_FAILED_RESTORE_FIRST
    gamma_timing lut_prepared
    # The production host independently checks READY and actual neutral RAM.
    "$GAMMA" log > "$RUN/gamma-request.txt" || { cat "$RUN/gamma-request.txt";exit 1; }
    gamma_timing native_log_return
    gamma_confirm LOG NATIVE_QUEUED "$RUN/gamma-request.txt"
    ;;
restore)
    result=$RUN/clean-log-restore.$$.txt
    if gamma_restore_request restore "$result";then
        gamma_timing native_restore_return
        # Factory Gamma recovery is always attempted before testing the
        # optional LUT engine identity. A broken engine cannot block it.
        engine_ok || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
        gamma_queue_confirm FACTORY FACTORY_QUEUED "$result"
        NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-release > "$RUN/clean-log-release.log" 2>&1 || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
        gamma_timing factory_lut_restored
        gamma_confirm FACTORY FACTORY_QUEUED
    else
        rc=$?;cat "$result";exit "$rc"
    fi
    ;;
lift)
    [ ! -e "$RUN/clean-log.policy" ] && [ ! -L "$RUN/clean-log.policy" ] || fail RESTORE_LOG_BEFORE_LIFT
    "$GAMMA" lift
    ;;
esac
