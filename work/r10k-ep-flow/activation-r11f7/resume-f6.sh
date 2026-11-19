#!/bin/sh
# Read-only ownership checks first. Keep the old payload/state and SD image.
OLD=$SYS/opt/usr/nxl-r11f6-activation
OLD_REPORT=$SD/NXDEMO51/R11F6_ACTIVATION_1794819981_295
OLD_SAVED=$SYS/etc/systemd/system/nxl-r11f7-preserved-f6.wants
old_retired=0
f6_regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
f6_pin(){ f6_regular "$1" && [ "$("$OPS" sha256 "$1")" = "$2" ]; }
f6_link(){ [ -L "$1" ] && [ "$(readlink "$1")" = "$2" ]; }
f6_inspect(){
 [ -d "$OLD_REPORT" ] && [ ! -L "$OLD_REPORT" ] || return 1
 f6_pin "$OLD_REPORT/activation-install.sh" 1b031912fc22cd46598dbcdb78c6fe7004dbd2d863d4d9b5dcb40f3eea35d9a7 || return 1
 f6_pin "$OLD_REPORT/entry-result.txt" 1df260ea79d3a5f899537e7330c6a8247bdfe98f62ec7f043dfb6adfdf79d622 || return 1
 f6_pin "$OLD_REPORT/history" 5fd45a026f30a22d13cbafcc5c9f25edf2209506cc5a0a5481370e69d49827ff || return 1
 f6_pin "$OLD_REPORT/request.boot" 45828f49f1caee27f6011fdd330e6fec32ba771e6d329c2ec412dab4958cb8e9 || return 1
 f6_pin "$OLD_REPORT/snapshot-backup.txt" 69b2bfdde238b681477557deef95e9b2faa7f910ff013a7ef61e3f91e6ddd628 || return 1
 f6_pin "$OLD_REPORT/snapshot-before.img" 46e69b6fc342885147a88831e2723ff87c282326ab457a80bbdd35b5742a3f5e || return 1
 [ "$(wc -c < "$OLD_REPORT/snapshot-before.img")" -eq 157285888 ] || return 1
 printf 'F6_FULL_BACKUP_VERIFIED=1\n' >> "$report/resume-f6.txt"
 if [ -e "$OLD" ] || [ -L "$OLD" ];then
  [ -d "$OLD" ] && [ ! -L "$OLD" ] && [ -d "$OLD/state" ] && [ ! -L "$OLD/state" ] || return 1
  f6_pin "$OLD/entry.sh" 1b031912fc22cd46598dbcdb78c6fe7004dbd2d863d4d9b5dcb40f3eea35d9a7 || return 1
  f6_pin "$OLD/payload.sha256" dbc49c2e78f2425b9d9b8214fde2ce22db427e8af4acc1a6a3dd08c3ebb7e841 || return 1
  [ "$(cat "$OLD/state/manifest.sha256")" = dbc49c2e78f2425b9d9b8214fde2ce22db427e8af4acc1a6a3dd08c3ebb7e841 ] || return 1
  manifest_root=$OLD;manifest_file=$OLD/payload.sha256;check_manifest || return 1
  f6_pin "$OLD/state/phase" 03cbb6c52e522ce55e217e3f9d66dbd66d21ef6bf85024016a3b7f3080c7944f || return 1
  f6_pin "$OLD/state/history" 5fd45a026f30a22d13cbafcc5c9f25edf2209506cc5a0a5481370e69d49827ff || return 1
  f6_pin "$OLD/state/request.boot" 45828f49f1caee27f6011fdd330e6fec32ba771e6d329c2ec412dab4958cb8e9 || return 1
  f6_regular "$OLD/state/report.path" && [ "$(cat "$OLD/state/report.path")" = "$OLD_REPORT" ] || return 1
  mkdir "$report/previous-r11f6-state" || return 1
  for item in "$OLD/state/"* "$OLD/state/".[!.]* "$OLD/state/"..?*;do
   [ -e "$item" ] || [ -L "$item" ] || continue
   f6_regular "$item" || return 1
   case "${item##*/}" in manifest.sha256|phase|history|request.boot|report.path) :;;preload.log) [ ! -s "$item" ] || return 1;;*) return 1;;esac
   cp "$item" "$report/previous-r11f6-state/" || return 1
  done
  printf 'F6_STATE=VERIFIED_FAILED_HANDOFF\n' >> "$report/resume-f6.txt"
 else printf 'F6_STATE=ABSENT_AFTER_RESTART\n' >> "$report/resume-f6.txt";fi
 [ ! -e "$OLD_SAVED" ] && [ ! -L "$OLD_SAVED" ] || return 1
 for leaf in nxl-r11f6-preload.service nxl-r11f6-post.service;do
  old_unit=$SYS/etc/systemd/system/$leaf;old_want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  for parent in "$SYS/etc/systemd/system" "$SYS/run/systemd/system" "$SYS/usr/lib/systemd/system";do
   [ ! -e "$parent/$leaf.d" ] && [ ! -L "$parent/$leaf.d" ] || return 1
   if [ "$parent" != "$SYS/etc/systemd/system" ];then [ ! -e "$parent/$leaf" ] && [ ! -L "$parent/$leaf" ] || return 1;fi
  done
  case "$leaf" in *preload*) old_pin=6851ec3d45367c977df96f91803eb3a6da484cd79b928c39386899ad2ed81b79;;*) old_pin=4d7044a1a86b0d0803ac75f0f1c9283aca486982176030e01cf7850a35a7b93e;;esac
  if [ -e "$old_unit" ] || [ -L "$old_unit" ];then f6_pin "$old_unit" "$old_pin" || return 1;fi
  if [ -e "$old_want" ] || [ -L "$old_want" ];then f6_link "$old_want" "../$leaf" && f6_pin "$old_unit" "$old_pin" || return 1;fi
  if [ -e "$OLD" ] || [ -e "$old_unit" ];then
   "$SYS/usr/bin/systemctl" show -p ActiveState -p SubState "$leaf" > "$report/$leaf.status" || return 1
   old_active=$(awk -F= '$1=="ActiveState" {print $2}' "$report/$leaf.status")
   old_sub=$(awk -F= '$1=="SubState" {print $2}' "$report/$leaf.status")
   case "$old_active:$old_sub:$leaf" in inactive:dead:*|failed:failed:*|active:exited:nxl-r11f6-preload.service) :;;*) return 1;;esac
  fi
 done
 printf 'F6_UNITS=OWNERSHIP_VERIFIED_AND_NO_WORKER\n' >> "$report/resume-f6.txt"
}
f6_retire(){
 mkdir "$OLD_SAVED" || return 1
 old_retired=1
 for leaf in nxl-r11f6-preload.service nxl-r11f6-post.service;do
  old_want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  if [ -e "$old_want" ] || [ -L "$old_want" ];then
   f6_link "$old_want" "../$leaf" || return 1
   mv "$old_want" "$OLD_SAVED/$leaf" || return 1
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
   [ ! -e "$old_want" ] && [ ! -L "$old_want" ] && f6_link "$OLD_SAVED/$leaf" "../$leaf" || return 1
   mv "$OLD_SAVED/$leaf" "$old_want" || return 1
  fi
 done
}
