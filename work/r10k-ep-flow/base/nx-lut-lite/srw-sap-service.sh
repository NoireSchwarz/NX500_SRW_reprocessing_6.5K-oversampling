#!/bin/sh
# R11l1: pin the OEM daemon itself; unit MainPID does not track its fork.
# Only normal TERM to the exact, twice-verified OEM SAP process is permitted.
# Never signal a process group, whitelist an EP peer, reset EP or use SIGKILL.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
SC=/usr/bin/systemctl
PROC=/proc
STATE=/tmp/nx-lut-lite
OUT=/mnt/mmc/NXDEMO51
UNIT=/usr/lib/systemd/system/sap.service
SAP=/usr/bin/sap-server-1.0.0
UNIT_DIGEST=f238f6fba8f0f4e12aa872e7e86e9279c36941d7a297782c80887b88bf486be9
SAP_DIGEST=81a6c6f429ed7e86c0845b7b274f270fe6dc73dcddfe4cf308684e50f7f67a4c
[ "$#" = 2 ] || exit 2
mode=$1; root=$2
case "$mode" in before|after) :;;*) exit 2;;esac
case "$root" in "$OUT"/INIT_AUTO_*) token=${root##*/INIT_AUTO_};;*) exit 2;;esac
[ "${#token}" = 32 ] && case "$token" in *[!0-9a-f]*) false;;*) true;;esac || exit 2
[ -d "$root" ] && [ ! -L "$root" ] && [ -d "$STATE" ] && [ ! -L "$STATE" ] || exit 2
lock=$STATE/srw-sap.lock
log(){ printf 'SRW_SAP %s\n' "$*" >> "$root/sap-service.log"; }
reason(){
 printf '%s\n' "$1" > "$root/sap-$mode.reason" || return 1
 [ "$mode" != before ] || printf '%s\n' "$1" > "$root/SAP.reason"
}
fail(){ log "$1";reason "$1";exit 1; }
hash(){ "$OPS" sha256 "$1"; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
unit_state(){
 "$SC" show -p ActiveState -p SubState -p MainPID -p ControlPID -p FragmentPath sap.service > "$root/sap-unit-current.txt" || return 1
 active=;sub=;main=;control=;fragment=
 while IFS= read -r line;do case "$line" in ActiveState=*)active=${line#*=};;SubState=*)sub=${line#*=};;MainPID=*)main=${line#*=};;ControlPID=*)control=${line#*=};;FragmentPath=*)fragment=${line#*=};;esac;done < "$root/sap-unit-current.txt"
 case "$main:$control" in *[!0-9:]*|:|:*)return 1;;esac
 [ -n "$main" ] && [ -n "$control" ] && [ "$fragment" = "$UNIT" ] && [ "$control" = 0 ]
}
unit_verified(){
 regular "$UNIT" && [ "$(hash "$UNIT")" = "$UNIT_DIGEST" ] && [ -x "$SC" ] && unit_state
}
owned(){
 [ -d "$lock" ] && [ ! -L "$lock" ] && regular "$lock/report" || return 1
 owner=;extra=;{ IFS= read -r owner && ! IFS= read -r extra && [ -z "$extra" ]; } < "$lock/report" || return 1
 [ "$owner" = "$root" ]
}
unlock(){ owned && rm "$lock/report" && rmdir "$lock"; }
# /proc/PID/stat fields after the known '(sap-server)' name contain no shell
# syntax. Disable pathname expansion while splitting those numeric fields.
peer_stat(){
 stat_line=;IFS= read -r stat_line < "$PROC/$1/stat" || return 1
 case "$stat_line" in "$1 (sap-server) "*) :;;*)return 1;;esac
 stat_tail=${stat_line#*) }
 set -f
 set -- $stat_tail
 set +f
 [ "$#" -ge 20 ] || return 1
 stat_state=$1;stat_parent=$2;stat_group=$3;stat_session=$4
 shift 19;stat_start=$1
 case "$stat_parent:$stat_group:$stat_session:$stat_start" in *[!0-9:]*|*::*|:*|*:)return 1;;esac
 [ "$stat_start" != 0 ] && [ "${#stat_start}" -le 20 ]
}
peer_read(){
 p=$1
 case "$p" in ''|*[!0-9]*|0|1)return 2;;esac
 peer_stat "$p" || { [ ! -d "$PROC/$p" ] && return 1;return 2; }
 case "$stat_state" in Z|X)return 1;;R|S|D|I) :;;*)return 2;;esac
 peer_start=$stat_start;peer_parent=$stat_parent;peer_group=$stat_group;peer_session=$stat_session
 comm=;IFS= read -r comm < "$PROC/$p/comm" || return 2
 [ "$comm" = sap-server ] && [ "$(readlink "$PROC/$p/exe")" = /usr/bin/sap-server-1.0.0 ] || return 2
 # fileops deliberately rejects symlinks for sha256. Pin the regular OEM
 # file, then use its existing streaming comparison to verify the live image.
 regular "$SAP" && [ "$(hash "$SAP")" = "$SAP_DIGEST" ] || return 2
 "$OPS" same "$PROC/$p/exe" "$SAP" || return 2
 # The captured owner is the original fork/setsid daemon. A directly managed
 # OEM main process is also accepted; unknown process/session shapes are not.
 if [ "$peer_parent:$peer_group:$peer_session" != "1:$p:$p" ];then
  [ "$active" = active ] && [ "$main" = "$p" ] || return 2
 fi
 tracer=;while IFS=' 	' read -r key value rest;do [ "$key" != TracerPid: ] || tracer=$value;done < "$PROC/$p/status"
 [ "$tracer" = 0 ] || return 2
 [ -r "$PROC/$p/cgroup" ] && [ -d "$PROC/$p/fd" ] || return 2
 printf 'PEER pid=%s start=%s parent=%s group=%s session=%s unit=%s main=%s sha256=%s\n' "$p" "$peer_start" "$peer_parent" "$peer_group" "$peer_session" "$active" "$main" "$SAP_DIGEST" >> "$root/sap-peer-audit.txt" || return 2
 cat "$PROC/$p/cgroup" >> "$root/sap-peer-audit.txt" || return 2
 peer_ep=0;peer_fds=
 for fd in "$PROC/$p/fd/"*;do
  [ -L "$fd" ] || continue
  target=$(readlink "$fd" 2>/dev/null) || { [ ! -L "$fd" ] && continue;return 2; }
  if [ "$target" = /dev/drime5_ep ];then
   peer_ep=$((peer_ep+1));peer_fds="$peer_fds ${fd##*/}"
  fi
 done
 printf 'EP_FDS pid=%s count=%s descriptors=%s\n' "$p" "$peer_ep" "$peer_fds" >> "$root/sap-peer-audit.txt" || return 2
 peer_stat "$p" || return 2
 [ "$stat_start:$stat_parent:$stat_group:$stat_session" = "$peer_start:$peer_parent:$peer_group:$peer_session" ] || return 2
 case "$stat_state" in Z|X|T|t)return 2;;esac
 [ "$(readlink "$PROC/$p/exe")" = /usr/bin/sap-server-1.0.0 ] || return 2
 return 0
}
scan(){
 count=0;found_pid=;found_start=;found_ep=0
 [ -r "$PROC/self/status" ] || return 2
 for dir in "$PROC"/[0-9]*;do
  [ -d "$dir" ] || continue
  pid=${dir##*/};case "$pid" in *[!0-9]*)continue;;esac
  comm=;if ! { IFS= read -r comm < "$dir/comm"; } 2>/dev/null;then continue;fi
  exe=$(readlink "$dir/exe" 2>/dev/null) || exe=
  [ "$comm" = sap-server ] || [ "$exe" = /usr/bin/sap-server-1.0.0 ] || continue
  r=0;peer_read "$pid" || r=$?
  case "$r" in 0) :;;1)continue;;*)return 2;;esac
  count=$((count+1));[ "$count" -le 1 ] || return 3
  found_pid=$pid;found_start=$peer_start;found_ep=$peer_ep
 done
 return 0
}
load_request(){
 regular "$root/sap-stop-requested" || return 1
 request_pid=;request_start=;request_version=
 while IFS= read -r line;do case "$line" in version=*)request_version=${line#*=};;pid=*)request_pid=${line#*=};;start=*)request_start=${line#*=};;*)return 1;;esac;done < "$root/sap-stop-requested"
 [ "$request_version" = NXSAPSTOP2 ] || return 1
 case "$request_pid:$request_start" in *[!0-9:]*|:|:*|*:)return 1;;esac
 [ "$request_pid" -gt 1 ] && [ "$request_start" != 0 ]
}
log 'VERSION=R11l1_PROCESS_LIFECYCLE2'
if [ "$mode" = after ];then
 if absent "$lock";then exit 0;fi
 owned || fail SAP_RESTORE_OWNER_MISMATCH
 # Never restart an EP client while the native job is held or unconfirmed.
 for h in auto-init-held engine.lock trace.lock memory-test.lock;do
  absent "$STATE/$h" || fail SAP_RESTORE_DEFERRED_NATIVE_HOLD
 done
 if absent "$root/sap-stop-requested";then unlock || fail SAP_UNLOCK_FAILED;exit 0;fi
 load_request || fail SAP_RESTORE_REQUEST_INVALID
 unit_verified || fail SAP_RESTORE_UNIT_IDENTITY_FAILED
 started=0;previous=;n=0
 while [ "$n" -lt 10 ];do
  unit_state || fail SAP_RESTORE_UNIT_VIEW_FAILED
  scan || fail SAP_RESTORE_PROCESS_IDENTITY_FAILED
  if [ "$count" = 1 ];then
   current=$found_pid:$found_start
   retire_pending=0
   if [ "$current" = "$request_pid:$request_start" ] && regular "$root/sap-term-intent" && ! regular "$root/sap-stop-not-sent";then retire_pending=1;fi
   if [ "$current" = "$previous" ] && [ "$retire_pending" = 0 ];then
    printf 'OEM_SAP_PROCESS_RESTORED pid=%s start=%s manager_state=%s\n' "$found_pid" "$found_start" "$active" > "$root/sap-service-restored.txt" || fail SAP_RESTORE_LOG_FAILED
    log "RESTORED actual_pid=$found_pid start=$found_start manager_state=$active duplicate_start=0" || exit 1
    reason SAP_RESTORED || exit 1
    unlock || fail SAP_UNLOCK_FAILED
    exit 0
   fi
   previous=$current
  else
   previous=
   if [ "$started" = 0 ];then
    case "$active:$main" in inactive:0|failed:0) :;;*)fail SAP_RESTORE_UNTRACKED_UNIT_PROCESS;;esac
    # One original start request only. Success is the actual stable daemon,
    # never ActiveState/MainPID (Type=simple exits when its parent forks).
    absent "$root/sap-start-requested" || fail SAP_RESTORE_PREVIOUS_START_PENDING
    printf 'OEM_SAP_START_REQUESTED\n' > "$root/sap-start-requested" || fail SAP_RESTORE_LOG_FAILED
    "$SC" --no-block start sap.service >> "$root/sap-manager.log" 2>&1 || fail SAP_RESTORE_START_FAILED
    started=1
   fi
  fi
  n=$((n+1));sleep 1
 done
 fail SAP_RESTORE_PROCESS_UNCONFIRMED
fi

# An earlier interrupted cleanup can be completed without another reset, but
# only through the same owned after path and its unchanged native-hold checks.
if ! absent "$lock";then
 [ -d "$lock" ] && [ ! -L "$lock" ] && regular "$lock/report" || fail SAP_PREVIOUS_OWNER_INVALID
 prior=;extra=;{ IFS= read -r prior && ! IFS= read -r extra && [ -z "$extra" ]; } < "$lock/report" || fail SAP_PREVIOUS_OWNER_INVALID
 [ "$prior" != "$root" ] || fail SAP_DUPLICATE_PREPARE
 /bin/sh "$0" after "$prior" || fail SAP_PREVIOUS_RESTORE_PENDING
 absent "$lock" || fail SAP_PREVIOUS_RESTORE_PENDING
fi
unit_verified || fail SAP_PREPARE_UNIT_IDENTITY_FAILED
cp "$root/sap-unit-current.txt" "$root/sap-unit-before.txt" || fail SAP_PREPARE_LOG_FAILED
r=0;scan || r=$?
case "$r" in 0) :;;3)fail SAP_MULTIPLE_PROCESSES;;*)fail SAP_PROCESS_IDENTITY_UNCONFIRMED;;esac
if [ "$count" = 0 ];then
 case "$active:$main" in inactive:0|failed:0)log 'UNCHANGED no_actual_SAP_process';reason SAP_READY_NO_PROCESS;exit 0;;*)fail SAP_UNIT_PROCESS_UNCONFIRMED;;esac
fi
if [ "$found_ep" = 0 ];then log "UNCHANGED actual_pid=$found_pid no_EP_descriptor";reason SAP_READY_NO_EP_DESCRIPTOR;exit 0;fi
selected=$found_pid;selected_start=$found_start
unit_verified || fail SAP_PREPARE_UNIT_CHANGED
scan || fail SAP_PREPARE_PROCESS_CHANGED
[ "$count:$found_pid:$found_start" = "1:$selected:$selected_start" ] && [ "$found_ep" -gt 0 ] || fail SAP_PREPARE_PROCESS_CHANGED
mkdir "$lock" || fail SAP_LOCK_EXISTS
printf '%s\n' "$root" > "$lock/report" || fail SAP_OWNER_LOG_FAILED
printf 'version=NXSAPSTOP2\npid=%s\nstart=%s\n' "$selected" "$selected_start" > "$root/sap-stop-requested" || fail SAP_STOP_LOG_FAILED
sync || fail SAP_STOP_LOG_SYNC_FAILED
log "TERM_REQUEST pid=$selected start=$selected_start OEM_executable_verified=1 unit_state=$active" || exit 1
printf 'OEM_SAP_TERM_INTENT\n' > "$root/sap-term-intent" || fail SAP_STOP_LOG_FAILED
sync || fail SAP_STOP_LOG_SYNC_FAILED
# Recheck identity after persisting the intent, immediately before TERM.
peer_read "$selected" || fail SAP_SIGNAL_IDENTITY_CHANGED
[ "$peer_start" = "$selected_start" ] && [ "$peer_ep" -gt 0 ] || fail SAP_SIGNAL_IDENTITY_CHANGED
if ! kill -TERM "$selected" 2>> "$root/sap-service.log";then
 printf 'OEM_SAP_TERM_NOT_SENT\n' > "$root/sap-stop-not-sent" || fail SAP_STOP_LOG_FAILED
 fail SAP_TERM_REQUEST_FAILED
fi
printf 'OEM_SAP_TERM_ACCEPTED\n' > "$root/sap-stop-accepted" || fail SAP_STOP_LOG_FAILED
n=0
while [ "$n" -lt 5 ];do
 unit_state || fail SAP_STOP_UNIT_VIEW_FAILED
 scan || fail SAP_STOP_PROCESS_VIEW_FAILED
 if [ "$count" = 0 ];then
  # A second read after a scheduling interval catches immediate respawn.
  sleep 1
  unit_state && scan || fail SAP_STOP_CONFIRMATION_FAILED
  [ "$count" = 0 ] || fail SAP_RESPAWN_DURING_PREPARE
  printf 'OEM_SAP_PROCESS_AND_EP_DESCRIPTORS_RELEASED\n' > "$root/sap-service-stopped.txt" || fail SAP_STOP_LOG_FAILED
  log 'STOPPED actual_process_absent=1 original_EP_admission_still_required=1' || exit 1
  reason SAP_READY_PROCESS_RELEASED || exit 1
  exit 0
 fi
 [ "$found_pid:$found_start" = "$selected:$selected_start" ] || fail SAP_RESPAWN_DURING_PREPARE
 n=$((n+1));sleep 1
done
fail SAP_TERM_EXIT_UNCONFIRMED
