#!/bin/sh
# R11f7 activation state. SD report paths are data, never sourced.
SYS=${SYS-}
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
H=$SYS/opt/usr/nxl-r11f7-activation
STATE=$H/state
OPS=$H/patch-fileops
SD=$SYS/mnt/mmc
ROOT=${SYS:-/}
OEM=b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8
PATCH=7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9
OPS_ID=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
sha(){ "$OPS" sha256 "$1"; }
phase_read(){ regular "$STATE/phase" && cat "$STATE/phase"; }
phase_write(){
 case "$1" in PREPARED|REFRESH_REQUESTED|PRELOAD_IN_PROGRESS|PRELOADED|VERIFIED|FAILED|ROLLBACK_REQUESTED|ROLLBACK_PRELOADED|ROLLED_BACK) :;;*) return 1;;esac
 [ ! -L "$STATE" ] && [ -d "$STATE" ] && [ ! -L "$STATE/phase" ] || return 1
 attempt=0;part=$STATE/phase.part.$$.0
 while [ -e "$part" ] || [ -L "$part" ];do
  attempt=$((attempt+1));[ "$attempt" -lt 128 ] || return 1;part=$STATE/phase.part.$$.$attempt
 done
 printf '%s\n' "$1" > "$part" && mv "$part" "$STATE/phase" || return 1
 printf '%s phase=%s\n' "$(date +%s)" "$1" >> "$STATE/history"
 sync
}
verify_payload(){
 [ ! -L "$H" ] && [ -d "$H" ] && regular "$OPS" && [ "$(sha "$OPS")" = "$OPS_ID" ] || return 1
 regular "$STATE/manifest.sha256" && regular "$H/payload.sha256" || return 1
 want=$(cat "$STATE/manifest.sha256")
 [ "${#want}" = 64 ] && [ "$(sha "$H/payload.sha256")" = "$want" ] || return 1
 count=0
 while IFS=' ' read -r digest leaf extra;do
  [ -z "$extra" ] && [ "${#digest}" = 64 ] || return 1
  case "$digest" in *[!0-9a-f]*) return 1;;esac
  case "$leaf" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) return 1;;esac
  count=$((count+1));[ "$count" -le 64 ] || return 1
  oldifs=$IFS;IFS=/;set -- $leaf;IFS=$oldifs;component=$H
  for part in "$@";do component=$component/$part;[ ! -L "$component" ] || return 1;done
  regular "$H/$leaf" && [ "$(sha "$H/$leaf")" = "$digest" ] || return 1
 done < "$H/payload.sha256"
 [ "$count" -ge 10 ]
}
report_copy(){
 regular "$STATE/report.path" || return 1
 report=$(cat "$STATE/report.path")
 case "$report" in "$SD"/NXDEMO51/R11F7_ACTIVATION_*) :;;*) return 1;;esac
 case "$report" in *..*) return 1;;esac
 [ -d "$report" ] && [ ! -L "$report" ] || return 1
 for leaf in phase history request.boot current.boot preload.log post.log post-check.txt commit-install.log commit-uninstall.log rollback-ready.txt rollback-counter-status.txt camera-service.txt loader-verify.txt loader-list.txt preload-process.txt process-check.txt rollback-process.txt recovery.log recovery-command.log recovery-result.txt resume-f6.txt;do
  if regular "$STATE/$leaf";then
   [ ! -L "$report/$leaf" ] || return 1
   cp "$STATE/$leaf" "$report/$leaf" || return 1
  fi
 done
}
notify(){
 case "$1" in WORKING) flag=--working;;VERIFIED) flag=--active;;ROLLED_BACK) flag=--restored;;*) flag=--failed;;esac
 ui=$H/nxl-activation-ui
 if [ -n "${RAM-}" ] && [ -x "$RAM/nxl-activation-ui" ];then ui=$RAM/nxl-activation-ui;fi
 if [ -x "$ui" ];then DISPLAY=:0 "$ui" "$flag" >/dev/null 2>&1 || :;fi
}
root_mode(){ "$OPS" mount-info "$SYS/proc/mounts" "$SYS/usr/lib/libmmfcore.so"; }

camera_find(){
 camera_state=UNAVAILABLE;camera_pid=0;camera_ticks=0;find_rc=0
 "$1" --find-camera > "$2" || find_rc=$?
 case "$find_rc" in 0) expected=ONE;;3) expected=NONE;;4) expected=AMBIGUOUS;;5) expected=UNAVAILABLE;;*) return 1;;esac
 grep -q '^NXL_R11F7_PROCESS1$' "$2" && grep -q '^end=NXL_R11F7_PROCESS_END1$' "$2" && grep -q "^state=$expected\$" "$2" || return 1
 camera_state=$expected
 if [ "$camera_state" = ONE ];then
  camera_pid=$(awk -F= '$1=="pid" {print $2}' "$2");camera_ticks=$(awk -F= '$1=="ticks" {print $2}' "$2")
  case "$camera_pid:$camera_ticks" in *[!0-9:]*|:*|*:) camera_state=UNAVAILABLE;return 1;;esac
  [ "${#camera_pid}" -le 10 ] && [ "${#camera_ticks}" -le 20 ] && [ "$camera_pid" -gt 1 ] && [ "$camera_ticks" != 0 ] || { camera_state=UNAVAILABLE;return 1; }
 fi
 case "$camera_state" in ONE|NONE) return 0;;*) return 1;;esac
}
prepare_recovery(){
 # No process/cwd/script descriptor may pin /opt/usr across rescue.
 REC=$SYS/run/nxl-r11f7-recovery
 [ ! -e "$REC" ] && [ ! -L "$REC" ] || return 1
 recovery_phase=$(phase_read) || return 1
 case "$recovery_phase" in REFRESH_REQUESTED|ROLLBACK_REQUESTED) :;;*) return 1;;esac
 [ -d "$SYS/run/systemd/system" ] && [ ! -L "$SYS/run/systemd/system" ] || return 1
 recovery_unit=$SYS/run/systemd/system/nxl-r11f7-recovery.service
 [ ! -e "$recovery_unit" ] && [ ! -L "$recovery_unit" ] || return 1
 mkdir "$REC" && chmod 700 "$REC" || return 1
 for leaf in common.sh recovery.sh patch-fileops nxl-activation-ui;do
  cp "$H/$leaf" "$REC/$leaf" && [ "$(sha "$H/$leaf")" = "$(sha "$REC/$leaf")" ] && chmod 700 "$REC/$leaf" || return 1
 done
 printf '%s\n' "$recovery_phase" > "$REC/expected-phase" || return 1
 cp "$STATE/request.boot" "$REC/request.boot" || return 1
 cp "$H/nxl-r11f7-recovery.service" "$recovery_unit" || return 1
 [ "$(sha "$H/nxl-r11f7-recovery.service")" = "$(sha "$recovery_unit")" ] || return 1
 "$SYS/usr/bin/systemctl" daemon-reload || return 1
 printf 'RECOVERY_HANDOFF_READY phase=%s\n' "$recovery_phase"
 sync
}
