#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
export NX_LUT_ROOT=$D DISPLAY=${DISPLAY:-:0}
. "$D/process.sh"
nxl_require_normal_mode || exit 1
/bin/sh "$D/lut.sh" recover-oled || exit 1
exec "$D/bin/nxl-menu" --toggle
