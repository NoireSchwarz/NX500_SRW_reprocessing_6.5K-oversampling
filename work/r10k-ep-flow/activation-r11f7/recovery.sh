#!/bin/sh
# Executed from tmpfs. The worker's unit survives rescue isolation.
set -u
SYS=
REC=$SYS/run/nxl-r11f7-recovery
. "$REC/common.sh"
OPS=$REC/patch-fileops
cd "$ROOT" || exit 70
mode=${1-};case "$mode" in launch|worker) :;;*) exit 71;;esac
exec >> "$REC/$mode.log" 2>&1
printf 'RECOVERY_%s_BEGIN pid=%s\n' "$mode" "$$"
# Termination during handoff is not proof of rejection. The next boot must
# still pass boot-id, executable, loader, inode and byte checks.
trap 'printf "RECOVERY_INTERRUPTED_PENDING mode=%s\n" "$mode";exit 0' HUP INT TERM
persist(){
 (
  cd "$STATE" || exit 1
  [ "$(cat "$SYS/proc/sys/kernel/random/boot_id")" = "$(cat "$REC/request.boot")" ] || exit 1
  [ "$(cat "$STATE/request.boot")" = "$(cat "$REC/request.boot")" ] || exit 1
  [ "$(phase_read)" = "$(cat "$REC/expected-phase")" ] || exit 1
  cp "$REC/result.txt" "$STATE/recovery-result.txt" || exit 1
  [ ! -f "$REC/command.log" ] || cp "$REC/command.log" "$STATE/recovery-command.log" || exit 1
  cp "$REC/$mode.log" "$STATE/recovery.log" || exit 1
  [ "$1" != FAILED ] || phase_write FAILED || exit 1
  report_copy || :
 )
}
reject(){
 printf 'state=REJECTED\nreason=%s\nexit_code=%s\n' "$1" "$2" > "$REC/result.txt"
 persist FAILED || :
 DISPLAY=:0 "$REC/nxl-activation-ui" --failed >> "$REC/notice.log" 2>&1 || :
 exit 1
}
[ "$(cat "$SYS/proc/sys/kernel/random/boot_id")" = "$(cat "$REC/request.boot")" ] || exit 72
[ "$(phase_read)" = "$(cat "$REC/expected-phase")" ] || exit 73
if [ "$mode" = launch ];then
 printf '%s\n' "$$" > "$REC/launcher.pid"
 "$SYS/usr/bin/systemctl" --no-block start nxl-r11f7-recovery.service || reject WORKER_START_REJECTED "$?"
 printf 'RECOVERY_WORKER_START_ACCEPTED; activation_unverified=1\n'
 exit 0
fi
printf 'state=DISPATCHING\nactivation_verified=0\n' > "$REC/result.txt"
persist PENDING || exit 74
# All state/SD descriptors and cwd from persist are closed before this call.
command_rc=0
"$SYS/usr/bin/systemctl" --no-block rescue > "$REC/command.log" 2>&1 || command_rc=$?
[ "$command_rc" = 0 ] || reject RECOVERY_COMMAND_REJECTED "$command_rc"
printf 'state=REQUEST_ACCEPTED\nactivation_verified=0\n' > "$REC/result.txt"
persist PENDING || :
printf 'RECOVERY_REQUEST_ACCEPTED; next_boot_verification_required=1\n'
exit 0
