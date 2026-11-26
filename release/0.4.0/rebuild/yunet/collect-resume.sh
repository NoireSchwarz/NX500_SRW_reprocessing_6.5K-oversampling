#!/bin/sh
# Record the first completed A-AF test window after the combined resume event.
set -u
trap '' HUP
umask 077
task_root=
sd=$task_root/mnt/mmc
run=$task_root/tmp/nx-lut-lite
pid=${1:-}
leaf=${2:-}
case "$pid" in ''|0|*[!0-9]*) exit 1;; esac
suffix=${leaf#FAST_START_EYE_AF_ROOTLIB_}
case "$suffix" in "$leaf"|''|*[!0-9_]*) exit 1;; esac

card_ready() {
    [ "$(cat "$task_root/sys/class/extcon/USB/state" 2>/dev/null)" = 0 ] &&
        awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") &&
             $4 ~ /(^|,)rw(,|$)/ {ok=1} END {exit !ok}' "$task_root/proc/mounts"
}

read_status() {
    status=$run/eye-af.status
    [ -f "$status" ] && [ ! -L "$status" ] && IFS= read -r line < "$status" || return 1
    case "$line" in
        "NXL_EYE_AF2 pid=$pid "*" model=yunet32-int8-v1 schema=28") ;;
        *) return 1;;
    esac
    extra=${line#* extra=}
    extra=${extra%% *}
    case "$extra" in ''|*[!0-9]*) return 1;; esac
    [ "$extra" -gt 0 ]
}

parent=$sd/NXDEMO51/$leaf/resume
card_ready && [ -d "$parent" ] && [ ! -L "$parent" ] &&
    [ ! -L "$sd/NXDEMO51" ] && [ ! -L "$sd/NXDEMO51/$leaf" ] || exit 1
done=$parent/yunet-af.complete
[ ! -e "$done" ] && [ ! -L "$done" ] || exit 0
# The camera may first be started without a lens. Wait for a real YuNet call,
# so that boot cannot consume the test before the lens and target are ready.
n=0
while card_ready && [ -d "$task_root/proc/$pid" ]; do
    if read_status; then break; fi
    [ "$n" -lt 45 ] || exit 0
    n=$((n+1))
    sleep 2
done
card_ready && [ -d "$task_root/proc/$pid" ] || exit 1
stamp=$(date +%s) || exit 1
report=$parent/yunet-af-$stamp-$$
mkdir "$report" || exit 1
boot=$(cat "$task_root/proc/sys/kernel/random/boot_id") || exit 1
activation=absent
for file in install.sh nx_cs.adj info.tg; do
    if [ -e "$sd/$file" ] || [ -L "$sd/$file" ]; then activation=present; fi
done
(set -C; printf 'boot_id=%s\ncamera_pid=%s\nactivation=%s\n' "$boot" "$pid" "$activation" > "$report/start.txt") || exit 1
(set -C; printf 'uptime_seconds\tstatus\n' > "$report/samples.tsv") || exit 1
last=
n=0
rows=0
while [ "$n" -lt 45 ] && card_ready && [ -d "$task_root/proc/$pid" ]; do
    if read_status && [ "$line" != "$last" ]; then
        IFS=' ' read -r up remaining < "$task_root/proc/uptime" || exit 1
        printf '%s\t%s\n' "$up" "$line" >> "$report/samples.tsv" || exit 1
        last=$line
        rows=$((rows+1))
    fi
    n=$((n+1))
    sleep 2
done
card_ready || exit 1
result=DONE
if [ "$n" -lt 45 ]; then result=CAMERA_PROCESS_EXITED; fi
if [ "$rows" -lt 2 ] && [ "$result" = DONE ]; then result=NO_FOLLOWUP_STATUS; fi
(set -C; printf 'result=%s\nrows=%s\n' "$result" "$rows" > "$report/result.txt") || exit 1
if [ "$result" = DONE ]; then
    (set -C; printf '%s\n' "${report##*/}" > "$done")
fi
