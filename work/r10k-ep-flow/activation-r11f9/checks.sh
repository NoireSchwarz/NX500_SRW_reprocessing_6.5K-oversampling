#!/bin/sh
# R11f9: observe the existing process. No process stop, memory write or recovery.
record(){
 printf 'check=%s\npath=%s\nresult=%s\nreturn_code=%s\nexpected=%s\nactual=%s\nend=R11F9_CHECK\n' "$1" "$2" "$3" "$4" "$5" "$6" >> "$report/checks.txt"
}
regular(){ [ -f "$1" ] && [ ! -L "$1" ]; }
directory(){ [ -d "$1" ] && [ ! -L "$1" ]; }
absent(){ [ ! -e "$1" ] && [ ! -L "$1" ]; }
equal(){
 if [ "$3" = "$4" ];then record "$1" "$2" PASS 0 "$3" "$4"
 else record "$1" "$2" WAIT 1 "$3" "$4" || :;return 1;fi
}
run(){
 ( tag=$1;output=$2;shift 2;code=0
   "$@" > "$output" 2>&1 || code=$?
   if [ "$code" = 0 ];then record "$tag" "$output" PASS 0 COMMAND_OK SEE_OUTPUT
   else record "$tag" "$output" WAIT "$code" COMMAND_OK SEE_OUTPUT || :;return "$code";fi )
}
pin(){
 ( path=$1;want=$2
   if ! regular "$path";then record FILE_TYPE "$path" WAIT 1 REGULAR MISSING_OR_LINK;return 1;fi
   code=0;got=$("$OPS" sha256 "$path" 2>> "$report/tool-stderr.log") || code=$?
   if [ "$code" != 0 ];then record FILE_SHA256 "$path" WAIT "$code" "$want" "${got:-NO_DIGEST}";return 1;fi
   equal FILE_SHA256 "$path" "$want" "$got" )
}
manifest(){
 ( root=$1;list=$2;limit=$3;count=0
   regular "$list" || return 1
   while IFS=' ' read -r digest leaf extra;do
    count=$((count+1));[ "$count" -le "$limit" ] && [ -z "$extra" ] && [ "${#digest}" = 64 ] || return 1
    case "$digest" in *[!0-9a-f]*) return 1;;esac
    case "$leaf" in ''|/*|*..*|*[!A-Za-z0-9_./-]*) return 1;;esac
    saved=$IFS;IFS=/;set -- $leaf;IFS=$saved;component=$root
    for part in "$@";do component=$component/$part;[ ! -L "$component" ] || return 1;done
    pin "$root/$leaf" "$digest" || return 1
   done < "$list"
   [ "$count" -ge 5 ] )
}
read_equal(){
 ( value=$(cat "$2" 2>> "$report/tool-stderr.log") || { record FILE_READ "$2" WAIT 1 READABLE ERROR;return 1; }
   equal "$1" "$2" "$3" "$value" )
}
context(){
 context_id=$1;code=0
 cat "$SYS/proc/sys/kernel/random/boot_id" > "$report/$context_id.boot" 2> "$report/$context_id.boot-error" || code=$?
 if [ "$code" != 0 ];then reason=BOOT_ID_READ_ERROR;record BOOT_ID "$report/$context_id.boot" WAIT "$code" UUID READ_ERROR;return 1;fi
 current_boot=$(cat "$report/$context_id.boot") || return 1
 case "$current_boot" in ????????-????-????-????-????????????) :;;*) reason=BOOT_ID_INVALID;record BOOT_ID "$report/$context_id.boot" WAIT 1 UUID "$current_boot";return 1;;esac
 case "$current_boot" in *[!0-9a-f-]*) reason=BOOT_ID_INVALID;return 1;;esac
 cat "$SYS/proc/uptime" > "$report/$context_id.uptime" 2>> "$report/tool-stderr.log" || { reason=UPTIME_READ_ERROR;return 1; }
 code=0;cat "$SYS/opt/usr/nxl-r11f8-activation/state/request.boot" > "$report/$context_id.request.boot" 2>> "$report/tool-stderr.log" || code=$?
 record PRIOR_BOOT_CONTEXT "$report/$context_id.request.boot" OBSERVED "$code" READ_ONLY SEE_OUTPUT || return 1
 # Record target state without changing it. Acceptance alone never proves loading.
 for target in rescue.target default.target di-camera-app.service;do
  code=0;"$SYS/usr/bin/systemctl" show -p ActiveState -p SubState "$target" > "$report/$context_id.$target" 2>&1 || code=$?
  record TARGET_CONTEXT "$report/$context_id.$target" OBSERVED "$code" READ_ONLY SEE_OUTPUT || return 1
  [ "$code" = 0 ] || { reason=TARGET_CONTEXT_UNAVAILABLE;return 1; }
  state=$(awk -F= '$1=="ActiveState" {print $2}' "$report/$context_id.$target") || return 1
  sub=$(awk -F= '$1=="SubState" {print $2}' "$report/$context_id.$target") || return 1
  case "$target:$state:$sub" in rescue.target:inactive:dead|default.target:active:active|di-camera-app.service:active:running) :;;*) reason=RECOVERY_OR_STARTUP_STILL_PENDING;return 1;;esac
 done
 return 0
}
idle(){
 reason=CAMERA_NOT_READY
 run CAMERA_READY "$report/$1.ready" "$D/bin/nxl-video-probe.arm" --ready || return 1
 reason=COUNTERS_NOT_OFF
 run COUNTER_STATUS "$report/$1.counter" "$D/bin/nxl-video-probe.arm" --status || return 1
 grep -q '^state=OFF$' "$report/$1.counter" || return 1
 return 0
}
live(){
 label=$1;reason=PROCESS_DISCOVERY_PENDING
 run PROCESS_DISCOVERY "$report/$label.process" "$PKG/nxl-gate-check.arm" --find-camera || return 1
 grep -q '^NXL_R11F8_PROCESS1$' "$report/$label.process" && grep -q '^state=ONE$' "$report/$label.process" && grep -q '^end=NXL_R11F8_PROCESS_END1$' "$report/$label.process" || { reason=PROCESS_PROTOCOL_INVALID;return 1; }
 camera_pid=$(awk -F= '$1=="pid" {print $2}' "$report/$label.process") || return 1
 camera_ticks=$(awk -F= '$1=="ticks" {print $2}' "$report/$label.process") || return 1
 case "$camera_pid:$camera_ticks" in *[!0-9:]*|:*|*:) reason=PROCESS_TOKEN_INVALID;return 1;;esac
 [ "${#camera_pid}" -le 10 ] && [ "${#camera_ticks}" -le 20 ] && [ "$camera_pid" -gt 1 ] && [ "$camera_ticks" != 0 ] || return 1
 reason=LIVE_GATE_NOT_VERIFIED
 run LIVE_GATE "$report/$label.gate" "$PKG/nxl-gate-check.arm" --check "$camera_pid" "$camera_ticks" || return 1
 grep -q '^NXL_R11F8_CHECK1$' "$report/$label.gate" && grep -q '^state=ACTIVE$' "$report/$label.gate" && grep -q '^reason=LIVE_CORE_AND_GATE_VERIFIED$' "$report/$label.gate" && grep -q '^command_enabled=0$' "$report/$label.gate" && grep -q '^end=NXL_R11F8_CHECK_END1$' "$report/$label.gate" || { reason=GATE_PROTOCOL_INVALID;return 1; }
 grep -q "^pid=$camera_pid\$" "$report/$label.gate" && grep -q "^ticks=$camera_ticks\$" "$report/$label.gate" || { reason=GATE_TOKEN_MISMATCH;return 1; }
 token=$camera_pid:$camera_ticks
 return 0
}
stable_live(){
 round=0;stable_token=
 context "$1-before" || return 1
 first_boot=$current_boot
 while [ "$round" -lt 3 ];do
  round=$((round+1));label=$1-$round
  idle "$label" && live "$label" || return 1
  if [ -n "$stable_token" ] && [ "$token" != "$stable_token" ];then reason=PROCESS_CHANGED_DURING_CHECK;return 1;fi
  stable_token=$token
  [ "$round" = 3 ] || sleep 1
 done
 context "$1-after" || return 1
 [ "$current_boot" = "$first_boot" ] || { reason=BOOT_CHANGED_DURING_CHECK;return 1; }
 # Equality with an OLD activation's boot ID is expressly allowed.
 return 0
}
