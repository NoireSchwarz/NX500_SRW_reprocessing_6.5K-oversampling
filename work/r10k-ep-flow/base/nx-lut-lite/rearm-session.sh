#!/bin/sh
# NX51_RESOURCE_REARM_R7N: current evidence, then owned markers, then a fresh job.
set -u
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
OUT=/mnt/mmc/NXDEMO51
RUN=/tmp/nx-lut-lite
OPS=$D/bin/nxl-fileops
SESSION=$D/bin/nxl-session.arm
PROBE=$D/bin/nxl-session-rearm.arm
MENU=$D/bin/nxl-menu
HOLDCHECK=$D/bin/nxl-holdcheck.arm
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
SESSION_SHA=70ca7a451d62abe4afb1d4ecd3c680e7b7b052916b723b7f9fb6d699c1cd4dea
PROBE_SHA=0d6eea845b7f90792a04712f031971ba689e98b366bff63b24b7b32e877c27bf
MENU_SHA=8bf174244b7588a494314d304b888912d7569d99cb08bd0830c80719033a4c9b
HOLDCHECK_SHA=0118e7de832ee1b0821e694d0b743e3b92c1e33bf9d1d857337cd026d532819d
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
hash(){ "$OPS" sha256 "$1"; }
nxl_progress(){ :; }
regular "$D/progress.sh" && . "$D/progress.sh"
for folder in "$OUT" "$OUT/logs";do [ ! -L "$folder" ] && { [ ! -e "$folder" ] || [ -d "$folder" ]; } || exit 1;[ -d "$folder" ] || mkdir "$folder" || exit 1;done
LOG=$OUT/logs/rearm.log
[ ! -L "$LOG" ] && { [ ! -e "$LOG" ] || regular "$LOG"; } || exit 1
exec >> "$LOG" 2>&1
printf 'NXREARM_WRAPPER revision=recovery-r8n-selector-confirm mode=%s pid=%s\n' "${1:-}" "$$"
reject(){ printf 'REARM_HELD reason=%s\n' "$1";nxl_progress RAW_RESOURCE_CHECK HELD "$1; see logs/rearm.log";exit 1; }
regular "$OPS" && [ -x "$OPS" ] && [ "$(hash "$OPS")" = "$OPS_SHA" ] || reject FILE_UTILITY_IDENTITY_FAILED
for pair in "SESSION:$SESSION_SHA" "PROBE:$PROBE_SHA" "MENU:$MENU_SHA" "HOLDCHECK:$HOLDCHECK_SHA";do
 name=${pair%%:*};want=${pair#*:};case "$name" in SESSION) file=$SESSION;;PROBE) file=$PROBE;;MENU) file=$MENU;;HOLDCHECK) file=$HOLDCHECK;;esac
 regular "$file" && [ -x "$file" ] && [ "$(hash "$file")" = "$want" ] || reject COMPONENT_IDENTITY_FAILED
done
regular "$D/process.sh" || reject PROCESS_HELPER_MISSING
. "$D/process.sh"
nxl_require_normal_mode || reject FACTORY_MODE_OR_PROCESS_VIEW_UNAVAILABLE
absent "$D/state/disabled" || reject MODULE_DISABLED
case "${1:-}" in
--rearm) [ "$#" = 1 ] || exit 2;exec "$SESSION" "$OUT" rearm "$0" --rearm-worker;;
--rearm-worker) [ "$#" = 1 ] && [ "${NX51_REARM_ONLY:-}" = 1 ] && [ "${NX51_SESSION_ROOT:-}" = "$OUT" ] || reject REARM_LEASE_REQUIRED;;
*) exit 2;;
esac
job=${NX51_JOB:-};previous=${NX51_PREVIOUS_JOB:-};token=${NX51_SESSION_TOKEN:-}
valid_job(){ case "$1" in "$OUT"/JOB_*) name=${1#"$OUT"/JOB_};;*) return 1;;esac;[ -n "$name" ] && [ "${#name}" -le 160 ] || return 1;case "$name" in *[!A-Za-z0-9_-]*) return 1;;esac;[ -d "$1" ] && [ ! -L "$1" ]; }
valid_job "$job" && valid_job "$previous" || reject INVALID_RESOURCE_JOB
[ "${#token}" = 32 ] && case "$token" in *[!0-9a-f]*) false;;*) true;;esac || reject INVALID_LEASE_TOKEN
preflight_workers_idle(){
 [ -r /proc/self/status ] || return 1;parent_supervisor=${PPID:-0};case "$parent_supervisor" in ''|0|*[!0-9]*) return 1;;esac
 for p in /proc/[0-9]*/status;do
  [ -r "$p" ] || continue;IFS=' 	' read -r label comm rest < "$p" || return 1;[ "$label" = Name: ] || return 1
  case "$comm" in nxl-demo-front*|nxl-native-fron*|nxl-demo-post*|nxl-hw-worker*|nxl-hw-controll*|nxl-oem-restor*|nxl-allocprobe*|nxl-bitbworker*|dfmsd) return 1;;nxl-session.arm) pid=${p%/status};[ "${pid##*/}" = "$parent_supervisor" ] || return 1;;esac
 done
}
proof=$job/rearm_$token
absent "$proof" || reject REARM_REPORT_ALREADY_EXISTS
cancelled=0;probe_pid=;popup=
cancel(){ cancelled=1;[ -z "$probe_pid" ] || kill -TERM "$probe_pid" 2>/dev/null || :; }
finish(){ [ -z "$probe_pid" ] || { kill -TERM "$probe_pid" 2>/dev/null || :;wait "$probe_pid" 2>/dev/null || :; };[ -z "$popup" ] || { kill -TERM "$popup" 2>/dev/null || :;wait "$popup" 2>/dev/null || :; }; }
trap cancel HUP INT TERM
trap finish 0
printf 'RESOURCE_CHECK source=%s target=%s report=%s\n' "$previous" "$job" "$proof"
nxl_progress RAW_RESOURCE_CHECK RUNNING "Checking current resources; report=$proof"
if [ "${NX51_PROGRESS_UI:-0}" != 1 ] && [ "${NX51_MENU_REARM_INLINE:-0}" != 1 ];then "$MENU" --demo-rearm-checking </dev/null >> "$LOG" 2>&1 & popup=$!;fi
rc=0;"$PROBE" --job "$job" --previous "$previous" --token "$token" --output "$proof" & probe_pid=$!
wait "$probe_pid" || rc=$?
[ "$cancelled" = 0 ] || exit 130
probe_pid=
if [ "$rc" != 0 ];then
 why=RESOURCE_PROOF_HELD
 if regular "$proof/RESET_DECISION.txt";then while IFS= read -r line;do case "$line" in reason=*) candidate=${line#reason=};case "$candidate" in ''|*[!A-Z0-9_]*) :;;*) [ "${#candidate}" -le 120 ] && why=$candidate;;esac;;esac;done < "$proof/RESET_DECISION.txt";fi
 nxl_progress RAW_RESOURCE_CHECK HELD "$why; report=$proof";exit "$rc"
fi
"$SESSION" "$OUT" verify-resource-proof "$proof" || reject RESOURCE_PROOF_CHANGED
preflight_workers_idle || reject NATIVE_WORKER_STILL_ACTIVE
"$HOLDCHECK" --check-nx51-pair "$previous" || reject WORKFLOW_HOLD_NOT_OWNED_BY_RESOURCE_JOB
reset_round_locks(){
 [ "$cancelled" = 0 ] || exit 130
 "$SESSION" "$OUT" verify-resource-proof "$proof" || reject RESOURCE_PROOF_CHANGED
 preflight_workers_idle || reject NATIVE_WORKER_STILL_ACTIVE
 "$HOLDCHECK" --check-nx51-pair "$previous" || reject WORKFLOW_HOLD_CHANGED
backup=$proof/round-locks.before
mkdir "$backup" || reject LOCK_BACKUP_CREATE_FAILED
for name in engine.lock trace.lock memory-test.lock demo50-recovery.lock;do
 lock=$RUN/$name;absent "$lock" && continue
 mkdir "$backup/$name" || reject LOCK_BACKUP_CREATE_FAILED
 for leaf in NX51_OWNER RESTART_REQUIRED NX51_ROUND;do absent "$lock/$leaf" && continue;regular "$lock/$leaf" && cp "$lock/$leaf" "$backup/$name/$leaf" && "$OPS" same "$lock/$leaf" "$backup/$name/$leaf" || reject LOCK_BACKUP_CHANGED;done
done
sync || reject LOCK_BACKUP_SYNC_FAILED
"$SESSION" "$OUT" verify-resource-proof "$proof" || reject RESOURCE_PROOF_CHANGED
preflight_workers_idle || reject NATIVE_WORKER_STILL_ACTIVE
"$HOLDCHECK" --check-nx51-pair "$previous" || reject WORKFLOW_HOLD_CHANGED
for name in engine.lock trace.lock memory-test.lock demo50-recovery.lock;do
 lock=$RUN/$name;absent "$lock" && continue
 for leaf in NX51_OWNER RESTART_REQUIRED NX51_ROUND;do
  if regular "$backup/$name/$leaf";then regular "$lock/$leaf" && "$OPS" same "$lock/$leaf" "$backup/$name/$leaf" || reject WORKFLOW_HOLD_CHANGED
  else absent "$lock/$leaf" || reject WORKFLOW_HOLD_CHANGED;fi
 done
 [ "$cancelled" = 0 ] || exit 130
 rm -f "$lock/NX51_OWNER" "$lock/RESTART_REQUIRED" "$lock/NX51_ROUND" && "$OPS" unlock "$lock" && absent "$lock" || reject ROUND_LOCK_RETIRE_FAILED
done
reset_receipt=$proof/LOCKS_RESET
(set -C;printf 'NX51LOCKRESET1\njob=%s\ntoken=%s\nengine=absent\ntrace=absent\nmemory_test=absent\nrecovery=absent\nnext_round=fresh_session_token\nend=NX51LOCKRESETEND1\n' "$job" "$token" > "$reset_receipt") || reject RESET_LOCK_RECEIPT_WRITE_FAILED
sync || reject RESET_LOCK_RECEIPT_SYNC_FAILED
}
nxl_progress CHECK_LOCKS RUNNING "Backing up and retiring this task round locks"
reset_round_locks
[ "$cancelled" = 0 ] || exit 130
nxl_progress CHECK_GRANT RUNNING "Renewing the verified front allocation permission"
"$SESSION" "$OUT" renew-front-grant "$proof" || reject FRONT_GRANT_RENEWAL_UNPROVEN
[ "$cancelled" = 0 ] || exit 130
nxl_progress CHECK_COMMIT RUNNING "Committing the new task and refreshing state"
"$SESSION" "$OUT" rearm-fresh "$proof" || reject RESOURCE_PROOF_COMMIT_REJECTED
nxl_progress RAW_RESOURCE_CHECK READY 'Fresh task ready; boot initialization may be reused if native preflight passes'
printf 'REARM_READY resource_proof=3 new_round_locks=fresh_token grant=READY source=%s\n' "$previous"
