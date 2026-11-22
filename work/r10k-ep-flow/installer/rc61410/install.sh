#!/bin/sh
# NXL_RC614_MODULE_ONLY_PRESERVE_WAKE_IMAGE1
trap '' HUP
task_root=
SD=$task_root/mnt/mmc
PKG=$SD/nx-lut-demo
OPS=$PKG/patch-fileops
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
ENTRY_SHA=2fee76b44689ee3d524dc512de2382d4b5cd92ff8238482b0ea98b987878a39c
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] || exit 70
[ -f "$PKG/rc614-module-entry.sh" ] && [ ! -L "$PKG/rc614-module-entry.sh" ] && [ "$("$OPS" sha256 "$PKG/rc614-module-entry.sh")" = "$ENTRY_SHA" ] || exit 70
exec /bin/sh "$PKG/rc614-module-entry.sh"
