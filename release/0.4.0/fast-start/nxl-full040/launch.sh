#!/bin/sh
# Preserve the stock executable, arguments and HIB. The claim is single-use.
task_root=
STATE=$task_root/opt/usr/nxl-full040
LIB=/usr/local/lib/nxl-full040/nxl-eye-af.so
APP=$task_root/usr/apps/com.samsung.di-camera-app/bin/di-camera-app

eligible() {
    [ -n "${HIB:-}" ] && [ -f "$STATE/armed" ] && [ ! -L "$STATE/armed" ] || return 1
    mode=$(cat "$task_root/sys/power/bootmode") || return 1
    case "$mode" in 1|3) ;; *) return 1;; esac
    awk '$1=="/dev/mmcblk0p9" {found=1} END {exit !found}' "$task_root/proc/swaps" || return 1
    { IFS= read -r before && IFS= read -r policy; } < "$STATE/armed" || return 1
    IFS= read -r now < "$task_root/proc/sys/kernel/random/boot_id" && [ -n "$now" ] && [ "$now" != "$before" ] || return 1
    case "$policy" in CONTROL|EYE) ;; *) return 1;; esac
    mapped=$APP
    if [ "$policy" = EYE ]; then mapped=$LIB; fi
    "$STATE/claim.arm" "$STATE" "$now" "$mode" "$$" "$policy" "$mapped"
}

cd / || exit 1
if eligible && [ "$policy" = EYE ]; then
    LD_PRELOAD=$LIB exec "$APP" "$@"
fi
exec "$APP" "$@"
