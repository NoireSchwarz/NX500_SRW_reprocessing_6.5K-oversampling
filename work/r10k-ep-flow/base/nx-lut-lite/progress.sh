#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only. Append display journal, no hardware authority.
nxl_progress(){
    printf 'PROGRESS phase=%s state=%s detail=%s\n' "${1:-}" "${2:-}" "${3:-}"
    [ "$#" = 3 ] || return 0
    nxlpg_writer=${NXL_PROGRESS_WRITER:-/opt/usr/nx-lut-lite/bin/nxl-session.arm}
    [ -f "$nxlpg_writer" ] && [ ! -L "$nxlpg_writer" ] && [ -x "$nxlpg_writer" ] || return 0
    "$nxlpg_writer" --publish-progress /mnt/mmc/NXDEMO51 "$1" "$2" "$3" </dev/null >/dev/null 2>&1 || :
    return 0
}
