#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export NX_LUT_ROOT=$D DISPLAY=${DISPLAY:-:0}
[ ! -e "$D/state/disabled" ] || { echo 'NX LUT Lite is disabled.' >&2; exit 1; }
. "$D/process.sh"
nxl_require_normal_mode || exit 1
nxl_require_no_keyscan || exit 1
# Diagnostic capture is explicit through TEST RECORDER / LENS RECORD START.
# Starting the listener must not create a persistent SD writer or500Hz observer.
/bin/sh "$D/lut.sh" recover-oled
# A damaged USB key must not become permanent across reboots. The helper
# restores only a matching captured value; leave the menu available on error.
/bin/sh "$D/video-mode.sh" usb-check || :
exec "$D/bin/nxl-keys" "$D/bin/nxl-menu"
