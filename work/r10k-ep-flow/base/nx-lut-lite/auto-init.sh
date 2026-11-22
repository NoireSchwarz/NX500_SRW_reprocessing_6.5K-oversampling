#!/bin/sh
# Automatic original configuration; passive lens evidence only in sampling profile.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
LOGPOLICY=$D/srw-log-policy.sh
SRW_LOG_POLICY_SHA=ea30b428f6a607c304b3a1400f2263a89f5e32bdb770dece39512c30efaf1093
OUT=/mnt/mmc/NXDEMO51
OPS=$D/bin/nxl-fileops
TEST=$D/bin/nxl-init-test.arm
CAPTURE=$D/lens-capture.sh
RUNNER=$D/memory-test.sh
LENS_EVIDENCE=0
SAP_GUARD=$D/srw-sap-service.sh
SAP_GUARD_SHA=25fe4b65714ff03d5c4ce2fe4ff66aa54b7f00f95ac4b1a9964c4646538de1f3
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
INIT_TEST_SHA=3b3f6fcf87234b5a7e4692ce2ce87e21ef1e9ceb2b956b61aac26711b1598718
LENS_CAPTURE_SHA=583dd0eab1984f1154f09128daa35e630d0b808e1e973f5eb38f3250578bbea3
RUNNER_SHA=b39229d3b0e9c4d14e4e5b1c926c6fe1a62e033086828b1be548ed62c19c29df
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
read_sap_reason(){
 sap_reason=$2;candidate=;extra=
 if regular "$root/sap-$1.reason" && { IFS= read -r candidate && ! IFS= read -r extra && [ -z "$extra" ]; } < "$root/sap-$1.reason";then
  case "$candidate" in ''|*[!A-Z0-9_]*) :;;*) if [ "${#candidate}" -le 100 ];then sap_reason=$candidate;return 0;fi;;esac
 fi
 return 1
}
sap_failure(){ read_sap_reason "$1" "$2" || :;fail "$sap_reason"; }
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
for pair in "$OPS:$OPS_SHA" "$SAP_GUARD:$SAP_GUARD_SHA" "$TEST:$INIT_TEST_SHA" "$CAPTURE:$LENS_CAPTURE_SHA" "$RUNNER:$RUNNER_SHA" "$LOGPOLICY:$SRW_LOG_POLICY_SHA";do
 file=${pair%:*};want=${pair##*:};regular "$file" && [ "$(hash "$file")" = "$want" ] || fail INIT_COMPONENT_IDENTITY_FAILED
done
regular "$D/process.sh" || fail PROCESS_HELPER_MISSING
. "$D/process.sh"
nxl_require_normal_mode || fail FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
absent /tmp/nx-lut-lite/auto-init-held || fail PREVIOUS_INIT_RETURN_UNCONFIRMED
absent /tmp/nx-lut-lite/lutload.pending || fail LUT_RELOAD_RETURN_UNCONFIRMED
[ -d "$OUT" ] && [ ! -L "$OUT" ] || fail REPORT_ROOT_UNAVAILABLE
root=$OUT/INIT_AUTO_$token
absent "$root" && mkdir "$root" || fail REPORT_ALREADY_EXISTS
/bin/sh "$LOGPOLICY" mark "$root" "$token" || fail LOG_POLICY_OWNER_FAILED
exec >> "$root/runner.log" 2>&1
# Preserve the last bounded LUT handoff proof in this existing operation;
# optional evidence cannot prevent or change original rendering.
if regular /tmp/nx-lut-lite/session/reload.log;then
 head -c 8192 /tmp/nx-lut-lite/session/reload.log > "$root/lut-reload-before.txt" || :
fi
sap_cleanup(){
 sap_exit=$?
 trap - 0
 if ! /bin/sh "$SAP_GUARD" after "$root";then
  read_sap_reason after SAP_RESTORE_UNCONFIRMED || :
  nxl_progress AUTO_SAP_RESTORE HELD "$sap_reason"
  [ "$sap_exit" != 0 ] || sap_exit=1
 fi
 exit "$sap_exit"
}
trap sap_cleanup 0
trap 'exit 130' 2
trap 'exit 143' 1 15
nxl_progress AUTO_SAP_PREPARE RUNNING 'Preparing the original accessory service for exclusive rendering'
/bin/sh "$SAP_GUARD" before "$root" || sap_failure before SAP_SERVICE_PREPARE_FAILED

trap 'exit 130' HUP INT TERM
nxl_progress AUTO_BASELINE RUNNING 'Reading cold Still state before original configuration'
before=0;"$TEST" --ready-idle "$root/before" || before=$?
case "$before" in 0|42) :;;*) probe_failure "$root/before" BASELINE_RAM_OR_IDLE_UNCONFIRMED;;esac
# Use the validated New Session resource transaction after a completed task.
/bin/sh "$RUNNER" --status > "$root/session-before.txt" || fail SESSION_STATUS_FAILED
state=
while IFS= read -r line;do case "$line" in state=*) state=${line#state=};;esac;done < "$root/session-before.txt"
original_job=$(sed -n 's/^job_dir=//p' "$root/session-before.txt")
rearmed=0
case "$state" in
 COMPLETE|RESTART_REQUIRED) /bin/sh "$RUNNER" --rearm > "$root/session-rearm.txt" 2>&1 || fail PREVIOUS_TASK_RESOURCE_CHECK_FAILED;rearmed=1;;
 RETRYABLE)
  # A consumed one-shot grant is not renewed by --new. In particular,
  # a file-only EXIF failure happens after the native request has returned.
  # Audit that round (or the recorded prior round for a zero-attempt retry)
  # before the existing supervisor may renew its allocation permission.
  prior_front=$(sed -n 's/^front_attempt=//p' "$root/session-before.txt")
  prior_back=$(sed -n 's/^backend_attempt=//p' "$root/session-before.txt")
  grant_ready=0
  if regular "$D/state/p49-experiment.state" && [ "$(hash "$D/state/p49-experiment.state")" = 78412039b6f52f39910799837f72ea30ee86171a5c1ab5ddd03dc6c3a3d6bc1a ];then grant_ready=1;fi
  if [ "$prior_front:$prior_back:$grant_ready" != 0:0:1 ];then
   nxl_progress RAW_RESOURCE_CHECK RUNNING 'Checking resources for initialization'
   /bin/sh "$RUNNER" --rearm > "$root/session-rearm.txt" 2>&1 || fail PREVIOUS_TASK_RESOURCE_CHECK_FAILED
   rearmed=1
  fi;;
 NEED_STILL_INIT) :;;
 *) fail CURRENT_TASK_NOT_ELIGIBLE;;
esac
# A single explicit button owns resource verification and a fresh round.
if [ -n "${NX51_SOURCE_SRW:-}" ];then
 selected_status=$root/session-before.txt
 if [ "$rearmed" = 1 ];then
  /bin/sh "$RUNNER" --status > "$root/session-prepared.txt" || fail PREPARED_SESSION_STATUS_FAILED
  grep -qx state=NEED_STILL_INIT "$root/session-prepared.txt" && grep -qx requires_reset=0 "$root/session-prepared.txt" || fail PREPARED_SESSION_NOT_READY
  selected_status=$root/session-prepared.txt
 fi
 current_job=$(sed -n 's/^job_dir=//p' "$selected_status")
 if [ "$rearmed" = 1 ] && [ -n "$current_job" ] && [ "$current_job" != "$original_job" ] &&
    grep -qx state=NEED_STILL_INIT "$selected_status" && grep -qx requires_reset=0 "$selected_status" &&
    grep -qx front_attempt=0 "$selected_status" && grep -qx backend_attempt=0 "$selected_status" &&
    grep -qx look_id=- "$selected_status";then
  printf 'REUSE_THIS_BUTTON_VERIFIED_FRESH_SESSION=%s\n' "$current_job" > "$root/session-selected-photo.txt"
 else
  /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED
  grep -qx state=NEED_STILL_INIT "$root/session-selected-photo.txt" && grep -qx requires_reset=0 "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_NOT_READY
 fi
 NX51_RC_NEW_JOB=1;export NX51_RC_NEW_JOB
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
/bin/sh "$SAP_GUARD" after "$root" || sap_failure after SAP_SERVICE_RESTORE_UNCONFIRMED
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
# Compact only this successful operation's optional wrapper logs. Native
# resource records remain available to the next button's rearm verifier.
completed_job=$(sed -n 's/^job_dir=//p' "$root/session-after.txt")
/bin/sh "$LOGPOLICY" complete "$completed_job" "$root" "$token" || printf 'OPTIONAL_LOG_COMPACTION_UNCONFIRMED\n' >&2
sync
nxl_progress AUTO_READY READY 'Automatic initialization complete; JPEG saved; workspace and EP reference returned.'
exit 0
