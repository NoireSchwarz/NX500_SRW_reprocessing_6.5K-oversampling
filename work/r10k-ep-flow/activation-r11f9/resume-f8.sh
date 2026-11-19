#!/bin/sh
# Exact known R11f8 failure is preserved. Only its verified wants links retire.
OLD=$SYS/opt/usr/nxl-r11f8-activation
OLD_REPORT=$SD/NXDEMO51/R11F8_ACTIVATION_1794844243_338
SAVED=$SYS/etc/systemd/system/nxl-r11f9-preserved-f8.wants
old_retired=0
link(){
 [ -L "$1" ] || { record LINK_TYPE "$1" WAIT 1 SYMLINK OTHER;return 1; }
 target=$(readlink "$1") || return 1
 equal LINK_TARGET "$1" "$2" "$target"
}
snapshot(){
 run COMPLETE_SNAPSHOT "$report/$1" "$PKG/nxl-snapshot-check.arm" --verify "$2" 157285888 "$3" || return 1
 grep -q '^state=VERIFIED$' "$report/$1" && grep -q '^end=NXL_R11F8_SNAPSHOT_CHECK_END1$' "$report/$1"
}
f8_inspect(){
 reason=PREVIOUS_F8_IDENTITY_NOT_VERIFIED
 directory "$OLD" && directory "$OLD/state" && directory "$OLD_REPORT" || return 1
 pin "$OLD/payload.sha256" 05e533bf4fd9a09a2b593b6fcc51849cc8f3eb9eda947f72763cbb3cd922d0ec || return 1
 pin "$OLD/entry.sh" 7f813c17af1e48b02c61eea4849124cd8fee02b6d7c45636f581db10212c4bf2 || return 1
 manifest "$OLD" "$OLD/payload.sha256" 19 || return 1
 read_equal F8_MANIFEST_STATE "$OLD/state/manifest.sha256" 05e533bf4fd9a09a2b593b6fcc51849cc8f3eb9eda947f72763cbb3cd922d0ec || return 1
 read_equal F8_REPORT_PATH "$OLD/state/report.path" "$OLD_REPORT" || return 1
 pin "$OLD/state/phase" 03cbb6c52e522ce55e217e3f9d66dbd66d21ef6bf85024016a3b7f3080c7944f || return 1
 pin "$OLD/state/history" 1c0bbd5b10d99e2990c5a1e3c47a6427f24a58f89ad6e9f79796e48cbc084b4f || return 1
 pin "$OLD/state/request.boot" f5dbe4d55eecd1745ebf25b133ef39e146f83679c828c2e6905fb57ecaf13398 || return 1
 pin "$OLD/state/resume-f6.txt" 1bbf4065836e59eff1577e2d18a8cf51cc95551e193123865f69e071ae812829 || return 1
 pin "$OLD/state/preload.log" b38501fe5b313adfa98b4703e72481a202c93cfce07f5be8f5d7e7f5239f427e || return 1
 pin "$OLD/state/recovery-result.txt" 6b36fc13a7aff4cf3292e7cbbba270af8387e916192f4e38f40b333700352186 || return 1
 pin "$OLD/state/recovery-command.log" e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 || return 1
 pin "$OLD/state/recovery.log" 6072acc330b769c5cdd2d34216ad9e686195e914868e86c8236d89680a761515 || return 1
 # State may contain diagnostics only from the known failure. Never erase it.
 for item in "$OLD/state/"* "$OLD/state/".[!.]* "$OLD/state/"..?*;do
  [ -e "$item" ] || [ -L "$item" ] || continue
  regular "$item" || return 1
  case "${item##*/}" in manifest.sha256|report.path|phase|history|request.boot|resume-f6.txt|preload.log|recovery-result.txt|recovery-command.log|recovery.log) :;;*) reason=UNKNOWN_F8_STATE_ITEM;record OLD_STATE_ITEM "$item" WAIT 1 KNOWN_F8_STATE UNKNOWN;return 1;;esac
 done
 mkdir "$report/previous-r11f8-state" || return 1
 for item in "$OLD/state/"*;do regular "$item" && cp "$item" "$report/previous-r11f8-state/" || return 1;done
 cp "$OLD/state/request.boot" "$report/previous-request.boot" || return 1
 reason=OLD_UNIT_OWNERSHIP_NOT_VERIFIED
 if ! absent "$SAVED";then
  directory "$SAVED" && pin "$H/payload.sha256" "$MANIFEST" || return 1
  for item in "$SAVED/"* "$SAVED/".[!.]* "$SAVED/"..?*;do
   [ -e "$item" ] || [ -L "$item" ] || continue
   case "${item##*/}" in nxl-r11f8-preload.service|nxl-r11f8-post.service) :;;*) return 1;;esac
  done
 fi
 for leaf in nxl-r11f8-preload.service nxl-r11f8-post.service;do
  unit=$SYS/etc/systemd/system/$leaf;want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  case "$leaf" in *preload*) digest=09b7c0d891b8c1809e127197bd30362290b8c0689a359536501e561986630885;;*) digest=489a8ea279ec44a4b91b98ce5871be872117626106881fb753377d5b970cdcac;;esac
  pin "$unit" "$digest" || return 1
  for parent in "$SYS/etc/systemd/system" "$SYS/run/systemd/system" "$SYS/usr/lib/systemd/system";do
   absent "$parent/$leaf.d" || return 1
   [ "$parent" = "$SYS/etc/systemd/system" ] || absent "$parent/$leaf" || return 1
  done
  if [ -L "$want" ];then link "$want" "../$leaf" && absent "$SAVED/$leaf" || return 1
  else absent "$want" && link "$SAVED/$leaf" "../$leaf" || return 1;fi
  run OLD_UNIT_STATUS "$report/$leaf.status" "$SYS/usr/bin/systemctl" show -p ActiveState -p SubState "$leaf" || return 1
  active=$(awk -F= '$1=="ActiveState"{print $2}' "$report/$leaf.status") || return 1
  sub=$(awk -F= '$1=="SubState"{print $2}' "$report/$leaf.status") || return 1
  case "$active:$sub:$leaf" in inactive:dead:*|failed:failed:*|active:exited:nxl-r11f8-preload.service) :;;*) reason=OLD_ACTIVATOR_STILL_RUNNING;return 1;;esac
 done
 run OLD_WORKER_STATUS "$report/old-recovery.status" "$SYS/usr/bin/systemctl" show -p ActiveState -p SubState nxl-r11f8-recovery.service || return 1
 active=$(awk -F= '$1=="ActiveState"{print $2}' "$report/old-recovery.status") || return 1
 sub=$(awk -F= '$1=="SubState"{print $2}' "$report/old-recovery.status") || return 1
 case "$active:$sub" in inactive:dead|failed:failed|active:exited) :;;*) reason=OLD_RECOVERY_STILL_RUNNING;return 1;;esac
 reason=COMPLETE_BACKUP_NOT_VERIFIED
 snapshot f8-snapshot-check.txt "$OLD_REPORT/snapshot-before.img" 9cd0150e18984a21af646239c1c671ba82e9e1c6a994beaa575b8d2cfe816fc7 || return 1
 snapshot f6-snapshot-check.txt "$SD/NXDEMO51/R11F6_ACTIVATION_1794819981_295/snapshot-before.img" 46e69b6fc342885147a88831e2723ff87c282326ab457a80bbdd35b5742a3f5e || return 1
 return 0
}
f8_retire(){
 if absent "$SAVED";then mkdir "$SAVED" || return 1;else directory "$SAVED" || return 1;fi
 old_retired=1
 for leaf in nxl-r11f8-preload.service nxl-r11f8-post.service;do
  want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
  if [ -L "$want" ];then
   link "$want" "../$leaf" && absent "$SAVED/$leaf" || return 1
   mv "$want" "$SAVED/$leaf" || return 1
  else absent "$want" || return 1;fi
  link "$SAVED/$leaf" "../$leaf" || return 1
 done
 record OLD_F8_WANTS_RETIRED "$SAVED" PASS 0 PRESERVED_WITHOUT_DELETING_OLD_STATE PRESERVED
}
f8_restore(){
 [ "$old_retired" = 1 ] || return 0
 for leaf in nxl-r11f8-preload.service nxl-r11f8-post.service;do
  if [ -L "$SAVED/$leaf" ];then
   want=$SYS/etc/systemd/system/di-camera-app.service.wants/$leaf
   absent "$want" && link "$SAVED/$leaf" "../$leaf" && mv "$SAVED/$leaf" "$want" || return 1
  fi
 done
 record OLD_F8_WANTS_RESTORED "$SAVED" PASS 0 ORIGINAL_LINKS_RESTORED RESTORED
}
