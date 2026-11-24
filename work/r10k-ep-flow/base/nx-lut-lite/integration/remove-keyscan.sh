#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
set -eu
TARGET=${1:-/opt/usr/nx-ks}
if [ -f "$TARGET/EV_AEL.sh.nxl-original" ]; then
    mv "$TARGET/EV_AEL.sh.nxl-original" "$TARGET/EV_AEL.sh"
elif [ -f "$TARGET/EV_AEL.sh.nxl-was-absent" ]; then
    rm -f "$TARGET/EV_AEL.sh" "$TARGET/EV_AEL.sh.nxl-was-absent"
fi
echo 'Original EV+AEL shortcut restored.'
