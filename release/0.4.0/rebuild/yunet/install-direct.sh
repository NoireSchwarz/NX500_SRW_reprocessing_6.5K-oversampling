#!/bin/sh
# Use the original SD callback and avoid a second service startup.
set -u
trap '' HUP
umask 077
unset LD_PRELOAD LD_BIND_NOW LD_LIBRARY_PATH DBUS_SYSTEM_BUS_ADDRESS DBUS_SESSION_BUS_ADDRESS
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
LC_ALL=C; export PATH LC_ALL
task_root=
SD=$task_root/mnt/mmc
PKG=$SD/nxl-full040-yunet2-direct
OPS=$PKG/fileops.arm

regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
pin() { regular "$1" && [ "$("$OPS" sha256 "$1")" = "$2" ]; }
card_up() {
    [ "$(cat "$task_root/sys/class/extcon/USB/state" 2>/dev/null)" = 0 ] &&
        awk '$1=="/dev/mmcblk1p1" && ($2=="/mnt/mmc" || $2=="/opt/storage/sdcard") && $4~/^rw(,|$)/ {ok=1} END {exit !ok}' "$task_root/proc/mounts"
}
card_up || exit 11
[ -d "$PKG" ] && [ ! -L "$PKG" ] && [ -d "$PKG/activation" ] && [ ! -L "$PKG/activation" ] || exit 70
pin "$OPS" c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d &&
    pin "$PKG/worker.sh" @WORKER_SHA@ || exit 70
for name in info.tg nx_cs.adj install.sh; do
    regular "$SD/$name" && regular "$PKG/activation/$name" &&
        "$OPS" same "$SD/$name" "$PKG/activation/$name" || exit 70
done
leaf=FAST_START_EYE_AF_ROOTLIB_$(date +%s)_$$
report=$SD/NXDEMO51/$leaf
[ ! -L "$SD/NXDEMO51" ] && mkdir -p "$SD/NXDEMO51" && mkdir "$report" && mkdir "$report/entry" || exit 70
# Disarm the original trigger first. Competing callbacks cannot both move it.
for name in info.tg nx_cs.adj install.sh; do
    card_up && mv "$SD/$name" "$report/entry/$name" || exit 70
done
card_up || exit 11
exec /bin/sh "$PKG/worker.sh" "$leaf"
