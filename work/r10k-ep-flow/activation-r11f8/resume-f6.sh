#!/bin/sh
# Preserve the known failed F6 installation and its complete snapshot.
OLD=$SYS/opt/usr/nxl-r11f6-activation
OLD_REPORT=$SD/NXDEMO51/R11F6_ACTIVATION_1794819981_295
OLD_SAVED=$SYS/etc/systemd/system/nxl-r11f8-preserved-f6.wants
old_retired=0
f6_record(){
 printf 'check=%s\npath=%s\nresult=%s\nreturn_code=%s\nexpected=%s\nactual=%s\nend=R11F8_RESUME_CHECK\n' "$1" "$2" "$3" "$4" "$5" "$6" >> "$report/resume-f6.txt"
}
f6_check(){
 ( tag=$1;where=$2;shift 2;rc=0;"$@" || rc=$?
   if [ "$rc" = 0 ];then f6_record "$tag" "$where" PASS 0 - -
   else f6_record "$tag" "$where" FAIL "$rc" - - || :;return "$rc";fi )
}
f6_absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
f6_regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
f6_directory(){ [ -d "$1" ] && [ ! -L "$1" ]; }
f6_equal(){
 if [ "$3" = "$4" ];then f6_record "$1" "$2" PASS 0 "$3" "$4"
 else f6_record "$1" "$2" FAIL 1 "$3" "$4" || :;return 1;fi
}
f6_read_equal(){
 ( code=0;value=$(cat "$2" 2>> "$report/f6-tool-stderr.log") || code=$?
   if [ "$code" != 0 ];then f6_record "$1" "$2" FAIL "$code" "$3" READ_FAILED || :;return 1;fi
   f6_equal "$1" "$2" "$3" "$value" )
}
f6_pin(){
 ( path=$1;want=$2
   f6_check REGULAR_FILE "$path" f6_regular "$path" || return 1
   count=UNKNOWN;count=$(wc -c < "$path") || { f6_record FILE_SIZE "$path" FAIL 1 READABLE UNKNOWN;return 1; }
   printf 'hash_path=%s\n' "$path" >> "$report/f6-tool-stderr.log" || return 1
   code=0;got=$("$OPS" sha256 "$path" 2>> "$report/f6-tool-stderr.log") || code=$?
   printf 'hash_bytes=%s\n' "$count" >> "$report/resume-f6.txt" || return 1
   if [ "$code" != 0 ];then f6_record FILE_SHA256 "$path" FAIL "$code" "$want" "${got:-NO_DIGEST}" || :;return 1;fi
   f6_equal FILE_SHA256 "$path" "$want" "$got" )
}
f6_link(){
 ( path=$1;want=$2
   f6_check SYMBOLIC_LINK "$path" test -L "$path" || return 1
   got=$(readlink "$path") || { f6_record READLINK "$path" FAIL 1 "$want" UNAVAILABLE;return 1; }
   f6_equal LINK_TARGET "$path" "$want" "$got" )
}
f6_snapshot(){
 ( path=$OLD_REPORT/snapshot-before.img;code=0
   f6_record SNAPSHOT_BEGIN "$path" BEGIN 0 '157285888 bytes; full SHA256' PENDING || return 1
   "$PKG/nxl-snapshot-check.arm" --verify "$path" 157285888 46e69b6fc342885147a88831e2723ff87c282326ab457a80bbdd35b5742a3f5e > "$report/f6-snapshot-check.txt" 2>&1 || code=$?
   if [ "$code" != 0 ];then f6_record SNAPSHOT_VERIFY "$path" FAIL "$code" EXACT_SIZE_AND_SHA256 'see f6-snapshot-check.txt' || :;return 1;fi
   if ! grep -q '^state=VERIFIED$' "$report/f6-snapshot-check.txt" || ! grep -q '^end=NXL_R11F8_SNAPSHOT_CHECK_END1$' "$report/f6-snapshot-check.txt";then
    f6_record SNAPSHOT_PROTOCOL "$path" FAIL 1 VERIFIED MALFORMED || :;return 1
   fi
   f6_record SNAPSHOT_VERIFY "$path" PASS 0 46e69b6fc342885147a88831e2723ff87c282326ab457a80bbdd35b5742a3f5e '157285888 bytes verified by production snapshot checker' )
}
f6_manifest(){
 ( count=0
   while IFS=' ' read -r digest leaf extra;do
    count=$((count+1))
    case "$digest" in *[!0-9a-f]*) f6_record MANIFEST_DIGEST "$OLD/payload.sha256" FAIL 1 SHA256 "$digest";return 1;;esac
    case "$leaf" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) f6_record MANIFEST_PATH "$OLD/payload.sha256" FAIL 1 SAFE_RELATIVE "$leaf";return 1;;esac
    if [ -n "$extra" ] || [ "${#digest}" != 64 ] || [ "$count" -gt 64 ];then f6_record MANIFEST_FORMAT "$OLD/payload.sha256" FAIL 1 VALID "line $count";return 1;fi
    oldifs=$IFS;IFS=/;set -- $leaf;IFS=$oldifs;component=$OLD
    for part in "$@";do component=$component/$part;f6_check COMPONENT_NOT_LINK "$component" test ! -L "$component" || return 1;done
    f6_pin "$OLD/$leaf" "$digest" || return 1
   done < "$OLD/payload.sha256"
   f6_equal MANIFEST_COUNT "$OLD/payload.sha256" 14 "$count" )
}
f6_inspect(){
 printf 'NXL_R11F8_RESUME1\n' > "$report/resume-f6.txt" || return 1
 f6_check OLD_F7_NOT_INSTALLED "$SYS/opt/usr/nxl-r11f7-activation" f6_absent "$SYS/opt/usr/nxl-r11f7-activation" || return 1
 f6_check OLD_REPORT_DIRECTORY "$OLD_REPORT" f6_directory "$OLD_REPORT" || return 1
 f6_pin "$OLD_REPORT/activation-install.sh" 1b031912fc22cd46598dbcdb78c6fe7004dbd2d863d4d9b5dcb40f3eea35d9a7 || return 1
 f6_pin "$OLD_REPORT/entry-result.txt" 1df260ea79d3a5f899537e7330c6a8247bdfe98f62ec7f043dfb6adfdf79d622 || return 1
 f6_pin "$OLD_REPORT/history" 5fd45a026f30a22d13cbafcc5c9f25edf2209506cc5a0a5481370e69d49827ff || return 1
 f6_pin "$OLD_REPORT/request.boot" 45828f49f1caee27f6011fdd330e6fec32ba771e6d329c2ec412dab4958cb8e9 || return 1
 f6_pin "$OLD_REPORT/snapshot-backup.txt" 69b2bfdde238b681477557deef95e9b2faa7f910ff013a7ef61e3f91e6ddd628 || return 1
 f6_snapshot || return 1
 printf 'F6_FULL_BACKUP_VERIFIED=1\n' >> "$report/resume-f6.txt" || return 1
 if [ -e "$OLD" ] || [ -L "$OLD" ];then
  f6_check OLD_DIRECTORY "$OLD" f6_directory "$OLD" || return 1
  f6_check OLD_STATE_DIRECTORY "$OLD/state" f6_directory "$OLD/state" || return 1
  f6_pin "$OLD/entry.sh" 1b031912fc22cd46598dbcdb78c6fe7004dbd2d863d4d9b5dcb40f3eea35d9a7 || return 1
  f6_pin "$OLD/payload.sha256" dbc49c2e78f2425b9d9b8214fde2ce22db427e8af4acc1a6a3dd08c3ebb7e841 || return 1
  f6_check OLD_MANIFEST_STATE "$OLD/state/manifest.sha256" f6_regular "$OLD/state/manifest.sha256" || return 1
  f6_read_equal MANIFEST_STATE "$OLD/state/manifest.sha256" dbc49c2e78f2425b9d9b8214fde2ce22db427e8af4acc1a6a3dd08c3ebb7e841 || return 1
  f6_check OLD_MANIFEST_ENTRIES "$OLD/payload.sha256" f6_manifest || return 1
  f6_pin "$OLD/state/phase" 03cbb6c52e522ce55e217e3f9d66dbd66d21ef6bf85024016a3b7f3080c7944f || return 1
  f6_pin "$OLD/state/history" 5fd45a026f30a22d13cbafcc5c9f25edf2209506cc5a0a5481370e69d49827ff || return 1
  f6_pin "$OLD/state/request.boot" 45828f49f1caee27f6011fdd330e6fec32ba771e6d329c2ec412dab4958cb8e9 || return 1
  f6_check REPORT_PATH_FILE "$OLD/state/report.path" f6_regular "$OLD/state/report.path" || return 1
  f6_read_equal REPORT_PATH "$OLD/state/report.path" "$OLD_REPORT" || return 1
  f6_check ARCHIVE_STATE_DIRECTORY "$report/previous-r11f6-state" mkdir "$report/previous-r11f6-state" || return 1
  for item in "$OLD/state/"* "$OLD/state/".[!.]* "$OLD/state/"..?*;do
   [ -e "$item" ] || [ -L "$item" ] || continue
   f6_check STATE_REGULAR_FILE "$item" f6_regular "$item" || return 1
   case "${item##*/}" in manifest.sha256|phase|history|request.boot|report.path) :;;preload.log) f6_check PRELOAD_LOG_EMPTY "$item" test ! -s "$item" || return 1;;*) f6_record STATE_ITEM "$item" FAIL 1 KNOWN_F6_STATE UNKNOWN_ITEM;return 1;;esac
   f6_check COPY_OLD_STATE "$item" cp "$item" "$report/previous-r11f6-state/" || return 1
  done
  printf 'F6_STATE=VERIFIED_FAILED_HANDOFF\n' >> "$report/resume-f6.txt" || return 1
 else printf 'F6_STATE=ABSENT_AFTER_RESTART\n' >> "$report/resume-f6.txt" || return 1;fi
 f6_check RETIRE_DIRECTORY_ABSENT "$OLD_SAVED" f6_absent "$OLD_SAVED" || return 1
 for leaf in nxl-r11f6-preload.service nxl-r11f6-post.service;do
  old_unit=$SYS/etc/systemd/system/$leaf;old_want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  for parent in "$SYS/etc/systemd/system" "$SYS/run/systemd/system" "$SYS/usr/lib/systemd/system";do
   f6_check NO_UNIT_OVERRIDE "$parent/$leaf.d" f6_absent "$parent/$leaf.d" || return 1
   if [ "$parent" != "$SYS/etc/systemd/system" ];then f6_check NO_ALTERNATE_UNIT "$parent/$leaf" f6_absent "$parent/$leaf" || return 1;fi
  done
  case "$leaf" in *preload*) old_pin=6851ec3d45367c977df96f91803eb3a6da484cd79b928c39386899ad2ed81b79;;*) old_pin=4d7044a1a86b0d0803ac75f0f1c9283aca486982176030e01cf7850a35a7b93e;;esac
  if [ -e "$old_unit" ] || [ -L "$old_unit" ];then f6_pin "$old_unit" "$old_pin" || return 1;fi
  if [ -e "$old_want" ] || [ -L "$old_want" ];then f6_link "$old_want" "../$leaf" && f6_pin "$old_unit" "$old_pin" || return 1;fi
  if [ -e "$OLD" ] || [ -e "$old_unit" ];then
   code=0;"$SYS/usr/bin/systemctl" show -p ActiveState -p SubState "$leaf" > "$report/$leaf.status" 2>&1 || code=$?
   if [ "$code" != 0 ];then f6_record UNIT_STATUS "$old_unit" FAIL "$code" IDLE 'see unit.status';return 1;fi
   code=0;old_active=$(awk -F= '$1=="ActiveState" {print $2}' "$report/$leaf.status") || code=$?
   if [ "$code" != 0 ];then f6_record UNIT_PARSE "$report/$leaf.status" FAIL "$code" ActiveState READ_FAILED;return 1;fi
   code=0;old_sub=$(awk -F= '$1=="SubState" {print $2}' "$report/$leaf.status") || code=$?
   if [ "$code" != 0 ];then f6_record UNIT_PARSE "$report/$leaf.status" FAIL "$code" SubState READ_FAILED;return 1;fi
   case "$old_active:$old_sub:$leaf" in inactive:dead:*|failed:failed:*|active:exited:nxl-r11f6-preload.service) f6_record UNIT_STATUS "$old_unit" PASS 0 IDLE "$old_active:$old_sub" || return 1;;*) f6_record UNIT_STATUS "$old_unit" FAIL 1 IDLE "$old_active:$old_sub";return 1;;esac
  fi
 done
 printf 'F6_UNITS=OWNERSHIP_VERIFIED_AND_NO_WORKER\n' >> "$report/resume-f6.txt"
}
f6_retire(){
 f6_check CREATE_RETIRE_DIRECTORY "$OLD_SAVED" mkdir "$OLD_SAVED" || return 1
 old_retired=1
 for leaf in nxl-r11f6-preload.service nxl-r11f6-post.service;do
  old_want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  if [ -e "$old_want" ] || [ -L "$old_want" ];then
   f6_link "$old_want" "../$leaf" || return 1
   f6_check RETIRE_OLD_LINK "$old_want" mv "$old_want" "$OLD_SAVED/$leaf" || return 1
   f6_link "$OLD_SAVED/$leaf" "../$leaf" || return 1
  fi
 done
 printf 'F6_WANTS_RETIRED_OLD_FILES_AND_STATE_PRESERVED=1\n' >> "$report/resume-f6.txt"
}
f6_restore_links(){
 [ "$old_retired" = 1 ] || return 0
 for leaf in nxl-r11f6-preload.service nxl-r11f6-post.service;do
  if [ -e "$OLD_SAVED/$leaf" ] || [ -L "$OLD_SAVED/$leaf" ];then
   old_want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
   f6_check RESTORE_DESTINATION_ABSENT "$old_want" f6_absent "$old_want" && f6_link "$OLD_SAVED/$leaf" "../$leaf" || return 1
   f6_check RESTORE_OLD_LINK "$old_want" mv "$OLD_SAVED/$leaf" "$old_want" || return 1
  fi
 done
}
