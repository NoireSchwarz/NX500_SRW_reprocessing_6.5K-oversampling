#!/bin/sh
# NX_LUT_LITE_BT_HOOK_V1
# NX_LUT_LITE_DEMO51_BT_HOOK_V1
# bt-start publishes SD diagnostics exclusively under NXDEMO51/logs.
# Preserve the stock daemon's arguments and foreground/daemon behaviour.
D=/opt/usr/nx-lut-lite
if [ -r "$D/bt-start.sh" ] && [ ! -e "$D/state/disabled" ]; then
    /bin/sh "$D/bt-start.sh" </dev/null >> "$D/state/startup.log" 2>&1 &
fi
exec /usr/sbin/bluetoothd.nxl-original "$@"
