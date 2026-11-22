#!/bin/sh
# Completion is not permission to skip the existing resource-proof rearm.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=/mnt/mmc/NXDEMO51
RUN=/tmp/nx-lut-lite
OPS=$D/bin/nxl-fileops
RUNNER=$D/memory-test.sh
LOGPOLICY=$D/srw-log-policy.sh
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
RUNNER_SHA=b39229d3b0e9c4d14e4e5b1c926c6fe1a62e033086828b1be548ed62c19c29df
SRW_LOG_POLICY_SHA=ea30b428f6a607c304b3a1400f2263a89f5e32bdb770dece39512c30efaf1093
fail(){ echo "ERR $*" >&2;exit 1; }
for pair in "$OPS:$OPS_SHA" "$RUNNER:$RUNNER_SHA" "$LOGPOLICY:$SRW_LOG_POLICY_SHA";do
 file=${pair%:*};want=${pair##*:};[ -f "$file" ] && [ ! -L "$file" ] && [ "$("$OPS" sha256 "$file")" = "$want" ] || fail FINISH_COMPONENT_IDENTITY_FAILED
done
[ "$#" = 0 ] || exit 2
token=${NX51_PROGRESS_TOKEN:-}
[ "${#token}" = 32 ] && case "$token" in *[!0-9a-f]*) false;;*) true;;esac || fail OPERATION_TOKEN_REQUIRED
mkdir -p "$RUN"
before=$RUN/rc1-session-before.$$
after=$RUN/rc1-session-after.$$
trap 'rm -f "$before" "$after"' 0 HUP INT TERM
shape(){ awk 'NR==1&&$0!="NX51STATUS1"{exit 1} $0=="NX51STATUS1"{h++} $0=="end=NX51END1"{e++} {last=$0} END{if(h!=1||e!=1||last!="end=NX51END1")exit 1}' "$1"; }
field(){ awk -v key="$2" 'index($0,key"=")==1{n++;value=substr($0,length(key)+2)} END{if(n!=1)exit 1;print value}' "$1"; }
/bin/sh "$RUNNER" --status > "$before" || fail COMPLETE_STATUS_UNAVAILABLE
shape "$before" && [ "$(field "$before" state)" = COMPLETE ] && [ "$(field "$before" front_verified)" = 1 ] || fail COMPLETION_NOT_VERIFIED
job=$(field "$before" job_dir) || fail JOB_IDENTITY_INVALID
case "$job" in "$OUT"/JOB_*) tail=${job#"$OUT"/JOB_};;*) fail JOB_SCOPE_INVALID;;esac
case "$tail" in ''|*[!A-Za-z0-9_-]*) fail JOB_SCOPE_INVALID;;esac
NX51_MENU_REARM_INLINE=1 /bin/sh "$RUNNER" --rearm > "$RUN/rc1-next-session.log" 2>&1 || fail NEW_SESSION_RESOURCE_PROOF_FAILED
/bin/sh "$RUNNER" --status > "$after" || fail NEW_SESSION_STATUS_UNAVAILABLE
shape "$after" && [ "$(field "$after" state)" = NEED_STILL_INIT ] && [ "$(field "$after" requires_reset)" = 0 ] && [ "$(field "$after" front_attempt)" = 0 ] && [ "$(field "$after" backend_attempt)" = 0 ] || fail NEW_SESSION_NOT_READY
next=$(field "$after" job_dir) || fail NEXT_JOB_IDENTITY_INVALID
case "$next" in "$OUT"/JOB_*) tail=${next#"$OUT"/JOB_};;*) fail NEXT_JOB_SCOPE_INVALID;;esac
case "$tail" in ''|*[!A-Za-z0-9_-]*) fail NEXT_JOB_SCOPE_INVALID;;esac
[ "$next" != "$job" ] || fail NEXT_JOB_NOT_FRESH
printf '%s\n' "$next" > "$RUN/rc1-next.job"
# Only optional text logs created by this RC operation can be compacted.
/bin/sh "$LOGPOLICY" complete "$job" "$OUT/INIT_AUTO_$token" "$token" || echo LOGS_RETAINED_COMPACTION_UNCONFIRMED
printf 'NXL_SRW_NEXT1\nready=1\njob=%s\nprevious=%s\nend=NXL_SRW_NEXT_END1\n' "$next" "$job"
