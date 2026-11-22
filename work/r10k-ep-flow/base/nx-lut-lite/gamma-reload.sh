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
GAMMA_LIFE_SHA=71c8e4c25e6f266491418929bd7767cba87bea67434d20b6cd9fa5fcaedbda19
LUT_IDLE_SHA=ca3d0c2e4ca05a45f5f6b145d7ce5a709bc48e854f02519e361123810f985f42
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
GAMMA_RELOAD_SHA=8ba4e7ae871445003e4624adb32a5116186d1c961f7c112c8a2ed7616b4b709e
USB_GUARD_SHA=61bf59b3bf4ccba775525e40024d0a42b86cffd22a1b94d9a431a8102f266918
ENGINE_SHA=c579e7ccaeace90da7a7cadc3041c5494a8b4d2921ea131234c348c4fb5b61cc
fail(){ printf 'NXL_GAMMA_RELOAD1\nmode=FACTORY\nphase=FAILED\nreason=%s\nepoch=0\nqueued_epoch=0\nqualified=0\napplies=0\ninvalidations=0\nqueues=0\nfault=0\nend=NXL_GAMMA_RELOAD_END1\n' "$1";exit 1; }
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
cleanup(){ rm -f "$lock/pid";rmdir "$lock" 2>/dev/null || :; }
trap cleanup 0
trap 'exit 130' HUP INT TERM
engine_ok(){ [ -f "$ENGINE" ] && [ ! -L "$ENGINE" ] && [ "$("$OPS" sha256 "$ENGINE")" = "$ENGINE_SHA" ]; }
gamma_wait(){
 wanted=$1;phase=$2;file=$RUN/gamma-confirm.txt
 [ -f "$IDLE" ] && [ ! -L "$IDLE" ] && [ -x "$IDLE" ] && [ "$("$OPS" sha256 "$IDLE")" = "$LUT_IDLE_SHA" ] || return 1
 for pass in 1 2 3 4 5 6 7 8;do
  if "$IDLE" --gamma-state > "$file" 2>&1 && grep -qx "mode=$wanted" "$file" && { grep -qx "phase=$phase" "$file" || { [ "$wanted" = FACTORY ] && grep -qx "phase=OFF" "$file"; }; } && grep -qx 'fault=0' "$file";then
   e=$(sed -n 's/^epoch=//p' "$file");q=$(sed -n 's/^queued_epoch=//p' "$file")
   [ -n "$e" ] && [ "$e" = "$q" ] && return 0
  fi
  sleep 1
 done
 return 1
}
gamma_queue_confirm(){
 wanted=$1;phase=$2
 if ! gamma_wait "$wanted" "$phase";then
  [ -f "$REFRESH" ] && [ ! -L "$REFRESH" ] && [ -x "$REFRESH" ] && [ "$("$OPS" sha256 "$REFRESH")" = "$NATIVE_REFRESH_SHA" ] || fail NATIVE_REFRESH_IDENTITY_FAILED
  "$REFRESH" photo > "$RUN/gamma-native-refresh.log" 2>&1 || fail GAMMA_NATIVE_REFRESH_UNCONFIRMED
  gamma_wait "$wanted" "$phase" || fail GAMMA_NATIVE_QUEUE_NOT_CONFIRMED
 fi
}
gamma_confirm(){
 gamma_queue_confirm "$1" "$2"
 # Keep the Gamma response as the final record, for the existing strict parser.
 /bin/sh "$ENGINE" reload > "$RUN/gamma-lut-reload.log" 2>&1 || fail GAMMA_LUT_RELOAD_UNCONFIRMED
 cat "$RUN/gamma-confirm.txt"
}
# Reconcile only on an explicit user control request, never on status/startup.
# A pending retirement must finish a native factory transaction and LUT cleanup
# before any subsequent Log request. Preserve all failures for the next retry.
[ -f "$LIFE" ] && [ ! -L "$LIFE" ] && [ -x "$LIFE" ] && [ "$("$OPS" sha256 "$LIFE")" = "$GAMMA_LIFE_SHA" ] || fail GAMMA_LIFECYCLE_IDENTITY_FAILED
"$LIFE" reconcile > "$RUN/gamma-life-reconcile.txt" 2>&1 || fail GAMMA_LIFECYCLE_RECONCILE_BLOCKED
if grep -qx 'outcome=RESTORE_FRESH_REQUIRED' "$RUN/gamma-life-reconcile.txt" || grep -qx 'outcome=RESTORE_REQUIRED' "$RUN/gamma-life-reconcile.txt";then
 if grep -qx 'outcome=RESTORE_FRESH_REQUIRED' "$RUN/gamma-life-reconcile.txt";then recover=restore-fresh;else recover=restore;fi
 "$GAMMA" "$recover" > "$RUN/gamma-life-factory-request.txt" || { cat "$RUN/gamma-life-factory-request.txt";exit 1; }
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
    # The production host independently checks READY and actual neutral RAM.
    "$GAMMA" log > "$RUN/gamma-request.txt" || { cat "$RUN/gamma-request.txt";exit 1; }
    gamma_confirm LOG NATIVE_QUEUED
    ;;
restore)
    result=$RUN/clean-log-restore.$$.txt
    if "$GAMMA" restore > "$result";then
        # Factory Gamma recovery is always attempted before testing the
        # optional LUT engine identity. A broken engine cannot block it.
        engine_ok || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
        gamma_queue_confirm FACTORY FACTORY_QUEUED
        NXL_LUT_DEFER_RELOAD=1 /bin/sh "$ENGINE" clean-log-release > "$RUN/clean-log-release.log" 2>&1 || fail CLEAN_LOG_GAMMA_RESTORED_LOOK_CLEANUP_REQUIRED
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
