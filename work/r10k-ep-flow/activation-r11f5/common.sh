#!/bin/sh
# R11f5 activation state. SD report paths are data, never sourced.
SYS=${SYS-}
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
H=$SYS/opt/usr/nxl-r11f5-activation
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
 case "$report" in "$SD"/NXDEMO51/R11F5_ACTIVATION_*) :;;*) return 1;;esac
 case "$report" in *..*) return 1;;esac
 [ -d "$report" ] && [ ! -L "$report" ] || return 1
 for leaf in phase history request.boot current.boot preload.log post.log post-check.txt commit-install.log commit-uninstall.log rollback-ready.txt rollback-counter-status.txt camera-service.txt loader-verify.txt loader-list.txt;do
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
