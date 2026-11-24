#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Optional existing-keyscan EV+Wi-Fi entry; invoke only after keys are released.
export NX_LUT_ROOT=/opt/usr/nx-lut-lite DISPLAY=${DISPLAY:-:0}
exec /opt/usr/nx-lut-lite/bin/nxl-menu --vlog-toggle
