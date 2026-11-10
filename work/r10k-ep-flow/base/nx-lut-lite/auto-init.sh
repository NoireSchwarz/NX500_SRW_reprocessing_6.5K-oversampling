#!/bin/sh
# Automatic original configuration; passive lens evidence only in sampling profile.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
OUT=/mnt/mmc/NXDEMO51
OPS=$D/bin/nxl-fileops
TEST=$D/bin/nxl-init-test.arm
CAPTURE=$D/lens-capture.sh
RUNNER=$D/memory-test.sh
LENS_EVIDENCE=1
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
INIT_TEST_SHA=0000000000000000000000000000000000000000000000000000000000000000
LENS_CAPTURE_SHA=0000000000000000000000000000000000000000000000000000000000000000
RUNNER_SHA=0000000000000000000000000000000000000000000000000000000000000000
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
hash(){ "$OPS" sha256 "$1"; }
nxl_progress(){ :; }
regular "$D/progress.sh" && . "$D/progress.sh"
fail(){
 if [ "$1" = FRONT_CODE_GUARD_REJECTED_NO_SUBMIT ] && [ -n "${root:-}" ] && regular "$root/ep-close/EP.returned" && absent /tmp/nx-lut-lite/auto-init-held;then
  nxl_progress AUTO_INIT_STOP RETRYABLE 'Firmware check stopped before processing; EP reference returned'
 else nxl_progress AUTO_INIT_STOP HELD "$1";fi
 exit 1
}
probe_failure(){
 file=$1/STATUS.reason;fallback=$2;candidate=;extra=
 if regular "$file" && { IFS= read -r candidate && ! IFS= read -r extra && [ -z "$extra" ]; } < "$file";then
  case "$candidate" in ''|*[!A-Z0-9_]*) :;;*) if [ "${#candidate}" -le 100 ];then fail "$candidate";fi;;esac
 fi
 fail "$fallback"
}
pipeline_failure(){
 for folder in ep-close ep-open;do
  file=$root/$folder/STATUS.reason
  if regular "$file";then
   candidate=;IFS= read -r candidate < "$file" || :
   case "$candidate" in ''|*[!A-Z0-9_]*|OWNED_EP_REFERENCE_READY_FOR_CONFIGURATION|OWNED_EP_REFERENCE_RETURNED) :;;*)
    if [ "${#candidate}" -le 100 ];then fail "$candidate";fi;;
   esac
  fi
 done
 probe_failure "$root" AUTO_PIPELINE_FAILED_SEE_JOB_AND_EP_REPORT
}
[ "$#" = 0 ] || exit 2
token=${NX51_PROGRESS_TOKEN:-}
[ "${#token}" = 32 ] && case "$token" in *[!0-9a-f]*) false;;*) true;;esac || fail PROGRESS_OPERATION_REQUIRED
for pair in "$OPS:$OPS_SHA" "$TEST:$INIT_TEST_SHA" "$CAPTURE:$LENS_CAPTURE_SHA" "$RUNNER:$RUNNER_SHA";do
 file=${pair%:*};want=${pair##*:};regular "$file" && [ "$(hash "$file")" = "$want" ] || fail INIT_COMPONENT_IDENTITY_FAILED
done
regular "$D/process.sh" || fail PROCESS_HELPER_MISSING
. "$D/process.sh"
nxl_require_normal_mode || fail FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
absent /tmp/nx-lut-lite/auto-init-held || fail PREVIOUS_INIT_RETURN_UNCONFIRMED
[ -d "$OUT" ] && [ ! -L "$OUT" ] || fail REPORT_ROOT_UNAVAILABLE
root=$OUT/INIT_AUTO_$token
absent "$root" && mkdir "$root" || fail REPORT_ALREADY_EXISTS
exec >> "$root/runner.log" 2>&1
trap 'exit 130' HUP INT TERM
nxl_progress AUTO_BASELINE RUNNING 'Reading cold Still state before original configuration'
before=0;"$TEST" --ready-idle "$root/before" || before=$?
case "$before" in 0|42) :;;*) probe_failure "$root/before" BASELINE_RAM_OR_IDLE_UNCONFIRMED;;esac
# Use the validated New Session resource transaction after a completed task.
/bin/sh "$RUNNER" --status > "$root/session-before.txt" || fail SESSION_STATUS_FAILED
state=
while IFS= read -r line;do case "$line" in state=*) state=${line#state=};;esac;done < "$root/session-before.txt"
case "$state" in
 COMPLETE|RESTART_REQUIRED) /bin/sh "$RUNNER" --rearm > "$root/session-rearm.txt" 2>&1 || fail PREVIOUS_TASK_RESOURCE_CHECK_FAILED;;
 NEED_STILL_INIT|RETRYABLE) :;;
 *) fail CURRENT_TASK_NOT_ELIGIBLE;;
esac
# Revalidate after a possible rearm transaction. Neither result grants a
if [ -n "${NX51_SOURCE_SRW:-}" ];then
 /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED
fi
# Each selected experiment has a fresh immutable source configuration.
# The verified rearm above still decides whether resource reuse is legal.
# native operation; the pipeline retains its original private preflight.
for held in auto-init-held engine.lock trace.lock memory-test.lock demo50-recovery.lock;do
 absent "/tmp/nx-lut-lite/$held" || fail INITIALIZATION_RESOURCE_HOLD_UNRESOLVED
done
again=0;"$TEST" --ready-idle "$root/recheck" || again=$?
case "$again" in 0|42) :;;*) probe_failure "$root/recheck" FINAL_IDLE_OR_READINESS_UNCONFIRMED;;esac
[ "$again" = "$before" ] || fail INITIALIZATION_STATE_CHANGED_DURING_CHECK
nxl_progress AUTO_PRECHECK_DONE RUNNING 'Current Still state and resources verified; continuing the shared pipeline'
if [ "$LENS_EVIDENCE" = 1 ];then
nxl_progress AUTO_RECORD RUNNING 'Starting the shared Still and lens recorder'
/bin/sh "$CAPTURE" start > "$root/recorder-start.txt" || fail LENS_RECORDER_UNAVAILABLE
/bin/sh "$CAPTURE" status > "$root/recorder-status.txt" || fail LENS_RECORDER_STATUS_UNAVAILABLE
recorder_line=;IFS= read -r recorder_line < "$root/recorder-status.txt" || fail LENS_RECORDER_STATUS_UNAVAILABLE
case "$recorder_line" in 'RECORDER_ACTIVE /mnt/mmc/NXDEMO51/INIT_LENS_'*) :;;*) fail LENS_RECORDER_NOT_ACTIVE;;esac
NX51_NATIVE_REPORT=${recorder_line#RECORDER_ACTIVE };export NX51_NATIVE_REPORT
/bin/sh "$CAPTURE" mark AUTO_COLD_BASELINE || fail LENS_BASELINE_MARK_FAILED
fi
NX51_INIT_REPORT=$root;export NX51_INIT_REPORT
if [ "$LENS_EVIDENCE" = 1 ];then
 /bin/sh "$CAPTURE" mark AUTO_CONFIG_PIPELINE_BEGIN || fail LENS_BEGIN_MARK_FAILED
 /bin/sh "$CAPTURE" yield-hardware "$token" > "$root/recorder-yield.txt" 2>&1 || fail LENS_HARDWARE_YIELD_UNCONFIRMED
fi
nxl_progress AUTO_EP_CYCLE RUNNING 'Original EP context, Still configuration, LUT and JPEG test'
rc=0;/bin/sh "$RUNNER" --init-trial raw422 100 || rc=$?
printf 'pipeline_exit=%s\n' "$rc" > "$root/pipeline-exit.txt"
# This last text record was repeatedly lost after a forced power-off while
# the native job was held. Flush before displaying the terminal result.
sync || printf 'PIPELINE_EXIT_SYNC_FAILED\n' >&2
if [ "$LENS_EVIDENCE" = 1 ];then /bin/sh "$CAPTURE" mark AUTO_CONFIG_PIPELINE_RETURNED || :;fi
[ "$rc" = 0 ] || pipeline_failure
regular "$root/ep-close/EP.returned" && absent /tmp/nx-lut-lite/auto-init-held || fail EP_REFERENCE_RETURN_UNCONFIRMED
nxl_progress AUTO_FINAL RUNNING 'Verifying Still readiness after the completed JPEG'
"$TEST" --readiness "$root/after" || fail FINAL_STILL_READINESS_FAILED
/bin/sh "$RUNNER" --status > "$root/session-after.txt" || fail FINAL_SESSION_STATUS_FAILED
state=
while IFS= read -r line;do case "$line" in state=*) state=${line#state=};;esac;done < "$root/session-after.txt"
[ "$state" = COMPLETE ] || fail PIPELINE_COMPLETION_NOT_COMMITTED
if [ "$LENS_EVIDENCE" = 1 ];then
/bin/sh "$CAPTURE" mark AUTO_INIT_AND_JPEG_COMPLETE || fail LENS_RECORDER_ENDED_BEFORE_TEST_COMPLETION
/bin/sh "$CAPTURE" status > "$root/recorder-final.txt" || fail LENS_RECORDER_FINAL_STATUS_FAILED
final_recorder=;IFS= read -r final_recorder < "$root/recorder-final.txt" || fail LENS_RECORDER_FINAL_STATUS_FAILED
[ "$final_recorder" = "$recorder_line" ] || fail LENS_RECORDER_CHANGED_DURING_TEST
fi
printf 'NXAUTOMENU1\ntoken=%s\noutcome=READY\nreport=%s\nend=NXAUTOMENUEND1\n' "$token" "$root" > "$root/MENU.result"
sync
nxl_progress AUTO_READY READY 'Automatic initialization complete; JPEG saved; workspace and EP reference returned.'
exit 0
