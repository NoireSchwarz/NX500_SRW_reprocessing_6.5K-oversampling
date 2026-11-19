#!/bin/sh
# NXL_STARTUP_READONLY_ENTRY1. This is a report collector, not an installer.
set -eu
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PATH
SYS=
SD=$SYS/mnt/mmc
OPS=$SD/nx-lut-demo/patch-fileops
PKG=$SD/nx-lut-startup-check
[ -d "$PKG" ] && [ ! -L "$PKG" ] && [ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] || exit 70
[ "$("$OPS" sha256 "$OPS")" = "c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d" ] || exit 71
[ "$("$OPS" sha256 "$PKG/collect.sh")" = "4e68b149ff055688bb068db672f8b4ff6accf1cb602cd65758b43c724b1a1110" ] || exit 72
"$OPS" version "$SYS/etc/version.info" || exit 73
exec /bin/sh "$PKG/collect.sh"
