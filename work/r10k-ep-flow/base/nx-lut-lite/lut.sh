#!/bin/sh
# NX_LUT_DYNAMIC24_V1
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
GUARD=$D/bin/nxl-lutguard.arm
CORE=$D/lut-core.sh
OPS_SHA=c5ab4fad18a3908f2c90058665b158c925267d88dd9c2e01c349fa9b149d759d
LUT_GUARD_SHA=07fb33c0c6d7d1279a9fb4cef8b8bc125143374fba3e8ff61273e6671f165fe9
LUT_CORE_SHA=11873fc54b28bbbbb1c6c230a86f73b099c07247d8388ef94134f58dbe837a89
die(){ echo "ERR $*" >&2;exit 1; }
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] || die LUT_COMPONENT_IDENTITY_FAILED
[ -f "$CORE" ] && [ ! -L "$CORE" ] && [ "$("$OPS" sha256 "$CORE")" = "$LUT_CORE_SHA" ] || die LUT_CORE_IDENTITY_FAILED
case "${1:-status}" in
list|status) exec /bin/sh "$CORE" "$@";;
esac
[ -f "$GUARD" ] && [ ! -L "$GUARD" ] && [ -x "$GUARD" ] && [ "$("$OPS" sha256 "$GUARD")" = "$LUT_GUARD_SHA" ] || die LUT_GUARD_IDENTITY_FAILED
exec "$GUARD" "$CORE" "$@"
