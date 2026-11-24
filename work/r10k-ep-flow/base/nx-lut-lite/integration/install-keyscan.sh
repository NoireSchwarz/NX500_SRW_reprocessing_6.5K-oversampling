#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Run only when an existing NX-KS/metol keyscan owns the shortcuts.
set -eu
D=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TARGET=${1:-/opt/usr/nx-ks}
case "$TARGET" in /*) ;; *) echo 'Use an absolute keyscan scripts directory' >&2; exit 1;; esac
[ -d "$TARGET" ] || { echo "Not found: $TARGET" >&2; exit 1; }
[ -x /opt/usr/nx-lut-lite/bin/nxl-menu ] || { echo 'First run the supplied installer in keyscan mode.' >&2; exit 1; }
if [ -e "$TARGET/EV_AEL.sh.nxl-original" ] || [ -e "$TARGET/EV_AEL.sh.nxl-was-absent" ]; then
    echo 'Already integrated; original preserved.'; exit 0
fi
[ ! -e "$TARGET/EV_AEL.sh" ] || cp -p "$TARGET/EV_AEL.sh" "$TARGET/EV_AEL.sh.nxl-original"
[ -e "$TARGET/EV_AEL.sh" ] || : > "$TARGET/EV_AEL.sh.nxl-was-absent"
cp "$D/integration/EV_AEL.sh" "$TARGET/EV_AEL.sh"
chmod 755 "$TARGET/EV_AEL.sh"
printf '%s\n' "$TARGET" > /opt/usr/nx-lut-lite/keyscan-target
echo "EV+AEL now opens NX LUT Lite via $TARGET/EV_AEL.sh"
echo 'If keyscan runs from SD, pass its actual scripts directory instead.'
