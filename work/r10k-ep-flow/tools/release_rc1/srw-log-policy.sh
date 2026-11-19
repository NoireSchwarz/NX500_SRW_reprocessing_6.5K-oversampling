#!/bin/sh
# Only successful RC1-created rounds may compact their optional text logs.
# Session journals, events.log, p39_commands.log and every proof/image remain.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT=/mnt/mmc/NXDEMO51
RUN=/tmp/nx-lut-lite
OPS=$D/bin/nxl-fileops
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
die(){ echo "ERR $*" >&2;exit 1; }
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] || die FILEOPS_IDENTITY_FAILED
token_ok(){ [ "${#1}" = 32 ] && case "$1" in *[!0-9a-f]*) false;;*) true;;esac; }
folder_ok(){
 case "$1" in "$OUT"/JOB_*) part=${1#"$OUT"/JOB_};;"$OUT"/INIT_AUTO_*) part=${1#"$OUT"/INIT_AUTO_};;*) return 1;;esac
 [ -n "$part" ] && [ "${#part}" -le 160 ] && case "$part" in *[!A-Za-z0-9_-]*) false;;*) true;;esac || return 1
 [ -d "$1" ] && [ ! -L "$1" ] || return 1
}
owned(){ folder_ok "$1" && [ -f "$1/.rc1-log-owner" ] && [ ! -L "$1/.rc1-log-owner" ] && [ "$(cat "$1/.rc1-log-owner")" = "NXL_RC1_LOGS1 token=$2" ]; }
case "${1:-}" in
mark)
 [ "$#" = 3 ] && token_ok "$3" && folder_ok "$2" || die INVALID_OWNED_FOLDER
 marker=$2/.rc1-log-owner
 [ ! -e "$marker" ] && [ ! -L "$marker" ] || { owned "$2" "$3";exit $?; }
 (set -C;printf 'NXL_RC1_LOGS1 token=%s\n' "$3" > "$marker") || die LOG_OWNER_SAVE_FAILED
 ;;
complete)
 [ "$#" = 4 ] && token_ok "$4" || die INVALID_LOG_REQUEST
 job=$2;init=$3;token=$4
 # A missing marker means a historical/foreign directory; leave it intact.
 owned "$job" "$token" && owned "$init" "$token" || { echo RETAINED_HISTORICAL_LOGS;exit 0; }
 mkdir -p "$RUN"
 list=$RUN/rc1-log-list.$$
 temp=$RUN/rc1-log-latest.$$
 trap 'rm -f "$list" "$temp"' 0 HUP INT TERM
 find "$job" "$init" -type f \( -name runner.log -o -name collector.log -o -name stdout.log -o -name stderr.log \) -print > "$list" || die LOG_ENUMERATION_FAILED
 printf 'NXL_RC1_LATEST_LOG1\ntoken=%s\njob=%s\noptional_text_logs_only=1\n' "$token" "$job" > "$temp"
 count=0
 while IFS= read -r file;do
  case "$file" in "$job"/*|"$init"/*) :;;*) die LOG_SCOPE_INVALID;;esac
  case "${file##*/}" in runner.log|collector.log|stdout.log|stderr.log) :;;*) die LOG_NAME_INVALID;;esac
  [ -f "$file" ] && [ ! -L "$file" ] || die LOG_SHAPE_CHANGED
  count=$((count+1));[ "$count" -le 64 ] || die LOG_COUNT_EXCEEDED
  printf '\n--- %s (first 8192 bytes) ---\n' "$file" >> "$temp"
  head -c 8192 "$file" >> "$temp" || die LOG_READ_FAILED
 done < "$list"
 latest=$OUT/logs/SRW_RC1_latest.log
 [ -d "$OUT/logs" ] && [ ! -L "$OUT/logs" ] || die LOG_DIRECTORY_INVALID
 [ ! -L "$latest" ] || die LATEST_LOG_SYMLINK
 if [ -e "$latest" ];then IFS= read -r prior < "$latest" || die LATEST_LOG_NOT_OWNED;[ "$prior" = NXL_RC1_LATEST_LOG1 ] || die LATEST_LOG_NOT_OWNED;fi
 staged=$latest.new.$$
 [ ! -e "$staged" ] && [ ! -L "$staged" ] || die LOG_STAGE_EXISTS
 cp "$temp" "$staged" && "$OPS" same "$temp" "$staged" && mv "$staged" "$latest" || die LATEST_LOG_SAVE_FAILED
 sync || die LATEST_LOG_SYNC_FAILED
 while IFS= read -r file;do
  case "$file" in "$job"/*|"$init"/*) :;;*) die LOG_SCOPE_INVALID;;esac
  case "${file##*/}" in runner.log|collector.log|stdout.log|stderr.log) :;;*) die LOG_NAME_INVALID;;esac
  [ -f "$file" ] && [ ! -L "$file" ] || die LOG_SHAPE_CHANGED
  rm -f "$file" || die LOG_COMPACT_FAILED
 done < "$list"
 printf 'NXL_RC1_LOG_POLICY1 compacted=%s latest=%s\n' "$count" "$latest"
 ;;
*) exit 2;;
esac
