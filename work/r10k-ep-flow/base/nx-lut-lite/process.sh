#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# Process checks use shell builtins only; no pidof/pgrep/ps dependency.
# Return 0: legacy keyscan found, 1: not found, 2: process view unavailable.
nxl_process_running() {
    [ -r /proc/self/status ] || return 2
    if ! IFS=' 	' read -r nxlps_label nxlps_name nxlps_rest < /proc/self/status; then
        return 2
    fi
    [ "$nxlps_label" = Name: ] || return 2
    for nxlps_file in /proc/[0-9]*/status; do
        [ -r "$nxlps_file" ] || continue
        # A process can exit between the glob and read. Skip that entry.
        if { IFS=' 	' read -r nxlps_label nxlps_name nxlps_rest < "$nxlps_file"; } 2>/dev/null; then
            [ "$nxlps_label" = Name: ] || continue
            for nxlps_wanted in "$@"; do
                [ "$nxlps_name" != "$nxlps_wanted" ] || return 0
            done
        fi
    done
    return 1
}
nxl_keyscan_running() { nxl_process_running keyscan keyscan1; }
nxl_require_no_keyscan() {
    if nxl_keyscan_running; then
        echo 'Existing keyscan detected. Use install.sh keyscan and integration/install-keyscan.sh.' >&2
        return 1
    else
        nxlps_result=$?
        [ "$nxlps_result" -eq 1 ] && return 0
        echo 'Cannot inspect running processes in /proc; standalone listener not started.' >&2
        return 1
    fi
}
nxl_require_normal_mode() {
    if nxl_process_running dfmsd; then
        echo 'Factory script mode (dfmsd) is active. Cold-restart before using LUT.' >&2
        return 1
    else
        [ "$?" -eq 1 ] || { echo 'Cannot inspect factory-mode process state.' >&2; return 1; }
    fi
    # NX cameras may resume a hibernation image. Do not infer normal mode from
    # a Linux boot UUID: inspect the actual factory daemon instead.
    return 0
}
# Called only as the final step of the SD bootstrap, after trigger cleanup and
# sync. Community NX scripts terminate dfmsd to remove its green-dot overlay.
nxl_finish_factory_install() {
    for nxlps_file in /proc/[0-9]*/status; do
        [ -r "$nxlps_file" ] || continue
        if { IFS=' 	' read -r nxlps_label nxlps_name nxlps_rest < "$nxlps_file"; } 2>/dev/null; then
            if [ "$nxlps_label" = Name: ] && [ "$nxlps_name" = dfmsd ]; then
                nxlps_pid=${nxlps_file%/status}; nxlps_pid=${nxlps_pid##*/}
                kill -TERM "$nxlps_pid" 2>/dev/null || :
            fi
        fi
    done
}
nxl_stop_listener() {
    for nxlps_file in /proc/[0-9]*/status; do
        [ -r "$nxlps_file" ] || continue
        if { IFS=' 	' read -r nxlps_label nxlps_name nxlps_rest < "$nxlps_file"; } 2>/dev/null; then
            if [ "$nxlps_label" = Name: ] && [ "$nxlps_name" = nxl-keys ]; then
                nxlps_pid=${nxlps_file%/status}; nxlps_pid=${nxlps_pid##*/}
                kill -TERM "$nxlps_pid" 2>/dev/null || :
            fi
        fi
    done
}
