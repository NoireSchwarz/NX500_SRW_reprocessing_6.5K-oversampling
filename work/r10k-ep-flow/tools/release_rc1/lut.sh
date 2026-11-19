#!/bin/sh
# NX_LUT_DYNAMIC24_V1
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OPS=$D/bin/nxl-fileops
GUARD=$D/bin/nxl-lutguard.arm
CORE=$D/lut-core.sh
OPS_SHA=0000000000000000000000000000000000000000000000000000000000000000
LUT_GUARD_SHA=0000000000000000000000000000000000000000000000000000000000000000
LUT_CORE_SHA=0000000000000000000000000000000000000000000000000000000000000000
die(){ echo "ERR $*" >&2;exit 1; }
[ -f "$OPS" ] && [ ! -L "$OPS" ] && [ -x "$OPS" ] && [ "$("$OPS" sha256 "$OPS")" = "$OPS_SHA" ] || die LUT_COMPONENT_IDENTITY_FAILED
[ -f "$CORE" ] && [ ! -L "$CORE" ] && [ "$("$OPS" sha256 "$CORE")" = "$LUT_CORE_SHA" ] || die LUT_CORE_IDENTITY_FAILED
case "${1:-status}" in
list|status) exec /bin/sh "$CORE" "$@";;
esac
[ -f "$GUARD" ] && [ ! -L "$GUARD" ] && [ -x "$GUARD" ] && [ "$("$OPS" sha256 "$GUARD")" = "$LUT_GUARD_SHA" ] || die LUT_GUARD_IDENTITY_FAILED
exec "$GUARD" "$CORE" "$@"
