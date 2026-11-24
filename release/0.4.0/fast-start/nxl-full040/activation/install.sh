#!/bin/sh
trap '' HUP
SD=/mnt/mmc
PKG=$SD/nxl-full040
OPS=$PKG/fileops.arm
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d ] || exit 70
WORK=$PKG/worker.sh
[ -f "$WORK" ] && [ ! -L "$WORK" ] && [ "$("$OPS" sha256 "$WORK")" = 690c3e78b06d9437c96678f4c8afd1dc2e3ecaadce6a719a40b854d39da02748 ] || exit 70
exec /bin/sh "$WORK"
