#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export NX_LUT_ROOT=$D
. "$D/process.sh"
nxl_require_normal_mode || exit 1
OPS=$D/bin/nxl-fileops
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
WIFI_SERVER_SHA=006a8415eae6f4bc8762e4e954fd5074f2973565b896c4057dc0d4e71ae409f0
RADIO=$D/bin/nxl-wifi-radio
RADIO_SHA=0b6ef6b97fbc87f74fa13226c2d45093c53e04bfde219d582bc2163d0e399c04
RADIO_TABLE_SHA=2c8ef0dd538d667af281312a8f6f2241de566bde59b97759f51f177c788789fe
WIFI_MANIFEST_SHA=859f7be631beffcd88a3a77616af1afdb2d6b281bc1452b77eebdfe2e3271c88
BIN=$D/bin/nxl-wifi-server
fail() {
 printf 'NXL_WIFI_ERROR1\nphase=%s\nreason=%s\nfile=%s\nexpected=%s\nactual=%s\nend=NXL_WIFI_ERROR_END1\n' "$1" "$2" "$3" "${4:-}" "${5:-}" >&2
 exit 1
}
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] || fail CHECKSUM_TOOL CHECKSUM_TOOL_UNAVAILABLE bin/nxl-fileops
actual=$("$OPS" sha256 "$OPS") || fail CHECKSUM_TOOL CHECKSUM_TOOL_UNAVAILABLE bin/nxl-fileops
[ "$actual" = "$OPS_SHA" ] || fail CHECKSUM_TOOL CHECKSUM_TOOL_MISMATCH bin/nxl-fileops "$OPS_SHA" "$actual"
check_file() {
 phase=$1;expected=$2;leaf=$3
 [ -f "$D/$leaf" ] && [ ! -L "$D/$leaf" ] || fail "$phase" WIFI_COMPONENT_MISSING "$leaf" "$expected"
 actual=$("$OPS" sha256 "$D/$leaf") || fail "$phase" WIFI_COMPONENT_UNREADABLE "$leaf" "$expected"
 [ "$actual" = "$expected" ] || fail "$phase" WIFI_COMPONENT_MISMATCH "$leaf" "$expected" "$actual"
}
stop_radio() {
 if "$RADIO" --stop;then return;fi
 fail RADIO_STOP RADIO_RESTORE_UNCONFIRMED bin/nxl-wifi-radio
}
# Verified stopping never depends on the webpage's state.
check_file SERVICE_BINARY "$WIFI_SERVER_SHA" bin/nxl-wifi-server
[ -x "$BIN" ] || fail SERVICE_BINARY WIFI_COMPONENT_NOT_EXECUTABLE bin/nxl-wifi-server
case "${1:-status}" in
 rvf-status) exec "$BIN" --rvf-status;;
 rvf-stop) exec "$BIN" --rvf-stop;;
esac
if [ "${1:-status}" = stop ];then
 "$BIN" --web-stop >/dev/null || exit 1
 check_file RADIO_BINARY "$RADIO_SHA" bin/nxl-wifi-radio
 stop_radio
 exec "$BIN" --stop
fi
if [ "${1:-status}" = status ];then exec "$BIN" --status;fi
manifest=$D/data/wifi.manifest
[ -f "$manifest" ] && [ ! -L "$manifest" ] || fail MANIFEST WIFI_MANIFEST_INVALID data/wifi.manifest
check_file MANIFEST "$WIFI_MANIFEST_SHA" data/wifi.manifest
seen_server=0;seen_process=0;seen_index=0;seen_app=0;seen_radio=0;seen_table=0;seen_extra=0
while IFS=' ' read -r expected leaf extra;do
 [ -z "$extra" ] && [ "${#expected}" = 64 ] || fail MANIFEST WIFI_MANIFEST_INVALID data/wifi.manifest
 case "$expected" in *[!0-9a-f]*) fail MANIFEST WIFI_MANIFEST_INVALID data/wifi.manifest;;esac
 case "$leaf" in
  bin/nxl-wifi-server) [ "$seen_server" = 0 ] && [ "$expected" = "$WIFI_SERVER_SHA" ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_server=1;;
  bin/nxl-wifi-radio) [ "$seen_radio" = 0 ] && [ "$expected" = "$RADIO_SHA" ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_radio=1;;
  wireless/cfg80211-cn-low5g.ko) [ "$seen_table" = 0 ] && [ "$expected" = "$RADIO_TABLE_SHA" ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_table=1;;
  process.sh) [ "$seen_process" = 0 ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_process=1;;
  wireless/web/index.html) [ "$seen_index" = 0 ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_index=1;;
  wireless/web/app.js) [ "$seen_app" = 0 ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_app=1;;
  wireless/web/extra.js) [ "$seen_extra" = 0 ] || fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";seen_extra=1;;
  *) fail MANIFEST WIFI_MANIFEST_INVALID "$leaf";;
 esac
 check_file RESOURCE "$expected" "$leaf"
done < "$manifest"
[ "$seen_server$seen_process$seen_index$seen_app$seen_radio$seen_table$seen_extra" = 1111111 ] || fail MANIFEST WIFI_MANIFEST_INVALID data/wifi.manifest
case "${1:-status}" in
 rvf)
  case "${2:-}" in ''|*[!0-9]*) echo RVF_WINDOW_UNAVAILABLE >&2;exit 1;;esac
  "$BIN" --rvf-serve "$D/wireless/web" "$2" </dev/null >/dev/null 2>&1 &
  child=$!;i=0
  while [ "$i" -lt 50 ];do
   if current=$("$BIN" --rvf-status "$child");then printf '%s\n' "$current";exit 0;fi
   kill -0 "$child" 2>/dev/null || break
   sleep 0.1;i=$((i+1))
  done
  kill "$child" 2>/dev/null || :
  echo RVF_START_FAILED >&2
  exit 1
  ;;
 status) exec "$BIN" --status;;
 connect) exec "$BIN" --connect;;
 hotspot|hotspot24|start)
  if [ "$1" = hotspot24 ];then
   "$RADIO" --stock-album-ready || fail RADIO_PREPARE STOCK_RADIO_UNCONFIRMED bin/nxl-wifi-radio
   "$BIN" --web-stop >/dev/null || exit 1
   if current=$("$BIN" --hotspot-start-24);then :;else
    result=$?
    "$BIN" --stop >/dev/null || fail HOTSPOT_STOP HOTSPOT_RESTORE_UNCONFIRMED bin/nxl-wifi-server
    printf '%s\n' "$current";exit "$result"
   fi
  elif [ "$1" = hotspot ];then
   "$BIN" --web-stop >/dev/null || exit 1
   if "$RADIO" --prepare;then :;else
    fail RADIO_PREPARE FIVE_GHZ_PREPARE_FAILED bin/nxl-wifi-radio
   fi
   if current=$("$BIN" --hotspot-start);then :;else
    result=$?
    stop_radio
    printf '%s\n' "$current";exit "$result"
   fi
   if ! "$RADIO" --confirm-profile;then
    stop_radio
    fail RADIO_PROFILE ACTIVE_PROFILE_48_40_UNCONFIRMED bin/nxl-wifi-radio
   fi
  fi
  old=$("$BIN" --status) || old=
  case "$old" in *'running=1'*) printf '%s\n' "$old";exit 0;;esac
  case "$old" in
   *'connected=1'*)
    "$BIN" --serve "$D/wireless/web" </dev/null >/dev/null 2>&1 &
    child=$!;i=0
    while [ "$i" -lt 20 ];do
     current=$("$BIN" --status) || break
     case "$current" in *'running=1'*) printf '%s\n' "$current";exit 0;;esac
     kill -0 "$child" 2>/dev/null || break
     sleep 0.1;i=$((i+1))
    done
    ;;
  esac
  if [ "$1" = hotspot ];then stop_radio;fi
  "$BIN" --stop >/dev/null || fail HOTSPOT_STOP HOTSPOT_RESTORE_UNCONFIRMED bin/nxl-wifi-server
  echo WIFI_START_FAILED >&2
  "$BIN" --status
  exit 1
  ;;
 *) echo WIFI_INVALID_COMMAND >&2;exit 2;;
esac
