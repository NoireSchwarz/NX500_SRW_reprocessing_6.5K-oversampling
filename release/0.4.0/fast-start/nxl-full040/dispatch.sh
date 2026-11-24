#!/bin/sh
# Root-backed worker. No open report, working directory or executable on /opt or SD.
task_root=
SD=$task_root/mnt/mmc
STATE=$task_root/opt/usr/nxl-full040
leaf=${1:-}
stamp=${leaf#FAST_START_EYE_AF_ROOTLIB_}
case "$stamp" in "$leaf"|''|*[!0-9_]*) exit 64;; esac
"$task_root/usr/bin/systemctl" rescue
rc=$?
printf 'systemctl_rescue_return=%s\n' "$rc" > "$task_root/tmp/nxl-eye-rootlib-rescue.return"
if [ "$rc" != 0 ] && [ ! -e "$STATE/launch-claim" ] && [ ! -L "$STATE/armed" ] &&
   [ "$(sed -n '3p' "$STATE/armed" 2>/dev/null)" = "$leaf" ] &&
   [ "$(sed -n '1p' "$STATE/armed")" = "$(cat "$task_root/proc/sys/kernel/random/boot_id")" ] &&
   [ ! -e "$task_root/opt/etc/.hib_capturing" ] &&
   awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {busy=1} END {exit !(ok && !busy)}' "$task_root/proc/swaps"; then
    mv "$STATE/armed" "$STATE/rescue-rejected"
fi
if [ "$(cat "$task_root/sys/class/extcon/USB/state" 2>/dev/null)" = 0 ] &&
   awk '$1=="/dev/mmcblk1p1" && ($2=="/opt/storage/sdcard" || $2=="/mnt/mmc") {ok=1} END {exit !ok}' "$task_root/proc/mounts" &&
   [ -d "$SD/NXDEMO51/$leaf" ] && [ ! -L "$SD/NXDEMO51/$leaf" ]; then
    printf 'systemctl_rescue_return=%s\n' "$rc" > "$SD/NXDEMO51/$leaf/rescue-return.txt"
fi
exit "$rc"
