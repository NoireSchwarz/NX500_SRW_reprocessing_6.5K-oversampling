#!/bin/sh
# Passive color observation: SD export only after recording stops.
set -eu
umask 077
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
COLOR=$D/bin/nxl-color-observe.arm
USB=$D/bin/nxl-usb-guard.arm
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
COLOR_OBSERVE_SHA=bfcb9126e3d0af6979a9ced64d6162bd1e7efc9bf5fc1e8199b17856571a3965
USB_GUARD_SHA=61bf59b3bf4ccba775525e40024d0a42b86cffd22a1b94d9a431a8102f266918
fail(){ printf 'NXL_COLOR_OBSERVE1\nstate=6\nsamples=0\nvalid=0\nreason=%s\nend=NXL_COLOR_OBSERVE_END1\n' "$1";exit 1; }
regular(){ [ -f "$1" ] && [ ! -L "$1" ] && [ -x "$1" ]; }
[ "$#" = 1 ] || exit 2
case "$1" in toggle|status) :;;*) exit 2;;esac
regular "$OPS" && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] && regular "$COLOR" && [ "$("$OPS" sha256 "$COLOR")" = "$COLOR_OBSERVE_SHA" ] && regular "$USB" && [ "$("$OPS" sha256 "$USB")" = "$USB_GUARD_SHA" ] || fail COLOR_COMPONENT_IDENTITY_FAILED
if [ "$1" = status ];then exec "$COLOR" --status;fi
. "$D/process.sh"
nxl_require_normal_mode || fail COLOR_FACTORY_SCRIPT_MODE
mkdir -p /tmp/nx-lut-lite
[ ! -L /tmp/nx-lut-lite ] || fail COLOR_PRIVATE_DIRECTORY_INVALID
current=$("$COLOR" --status) || fail COLOR_STATUS_FAILED
state=
while IFS= read -r line;do case "$line" in state=*) state=${line#state=};;esac;done <<EOF
$current
EOF
case "$state" in
1|2|3|4|6)
 current=$("$COLOR" --stop) || { printf '%s\n' "$current";exit 1; }
 tries=0
 while [ "$tries" -lt 8 ];do
  case "$current" in *'state=5'*) printf '%s\n' "$current";exit 0;;*'state=3'*|*'state=6'*) printf '%s\n' "$current";exit 1;;esac
  sleep 1;tries=$((tries+1));current=$("$COLOR" --status) || fail COLOR_STATUS_FAILED
 done
 printf '%s\n' "$current";exit 0;;
0|5) :;;*) fail COLOR_STATE_INVALID;;esac
"$USB" preflight BEFORE_COLOR >/dev/null 2>&1 || fail COLOR_UNPLUG_USB
[ -d /mnt/mmc/NXDEMO51 ] && [ ! -L /mnt/mmc/NXDEMO51 ] || fail COLOR_REPORT_DIRECTORY_INVALID
report=/mnt/mmc/NXDEMO51/COLOR_R11L_$(date +%s)_$$
mkdir "$report" || fail COLOR_REPORT_CREATE_FAILED
log=/tmp/nx-lut-lite/color-start-$$.txt
"$COLOR" --run "$report" </dev/null >"$log" 2>&1 &
pid=$!;tries=0
while [ "$tries" -lt 10 ];do
 current=$("$COLOR" --status) || current=
 case "$current" in *'state=1'*) printf '%s\n' "$current";exit 0;;esac
 kill -0 "$pid" 2>/dev/null || fail COLOR_START_FAILED_SEE_RAM_LOG
 sleep 1;tries=$((tries+1))
done
"$COLOR" --stop >/dev/null 2>&1 || :
fail COLOR_START_TIMEOUT
