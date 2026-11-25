#!/bin/sh
# Read the current capture manager before the OEM image writer runs.
# Use stdout or the selected build profile's RAM directory. A failure closes the gate.
set -u
LC_ALL=C
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export LC_ALL PATH
root=
ctl=$root/usr/bin/systemctl
event=hib_post.service
watch=hib_post.path
companion=nxl-full040-resume.service
helper=/opt/usr/nxl-full040/resume-after-stock.sh
stage=before-capture
if [ "$#" = 1 ] && [ "$1" = --when-capturing ]; then
    # These are the two exact no-write exits in the original hibernation.sh.
    # Other boot modes with the marker present must still pass the capture test.
    [ -e "$root/etc/snapshot" ] || exit 0
    if [ "$(cat "$root/sys/power/bootmode" 2>/dev/null)" = 4 ]; then exit 0; fi
    set -- --record
fi

case "$#/${1:-}" in
    0/) ;;
    1/--record|1/--at-image-write)
        [ "$1" != --at-image-write ] || stage=image-header
        proofdir=$root/run/nxl-full040-capture
        umask 077
        if [ ! -e "$proofdir" ] && [ ! -L "$proofdir" ]; then
            mkdir "$proofdir" || { [ -d "$proofdir" ] && [ ! -L "$proofdir" ]; } || exit 1
        fi
        [ -d "$proofdir" ] && [ ! -L "$proofdir" ] || exit 1
        receipt=$proofdir/$stage.$$.txt
        [ ! -e "$receipt" ] && [ ! -L "$receipt" ] || exit 1
        set -C
        exec > "$receipt" 2>&1
        ;;
    *) exit 2;;
esac

stop() { printf 'decision=STOP\nreason=%s\n' "$1"; exit 1; }
value() {
    printf '%s\n' "$1" | awk -v key="$2" 'index($0,key "=")==1 {
        count++; v=substr($0,length(key)+2)
    } END {if(count!=1)exit 1; print v}'
}
has_word() {
    printf '%s\n' "$1" | awk -v word="$2" '{for(i=1;i<=NF;i++)if($i==word)ok=1} END {exit !ok}'
}
loaded() {
    [ "$(value "$1" LoadState)" = loaded ] && [ "$(value "$1" NeedDaemonReload)" = no ]
}
query_event() {
    "$ctl" --all show "$event" -p LoadState -p Wants -p TriggeredBy -p NeedDaemonReload
}
snapshot_untouched() {
    [ -e "$root/etc/snapshot" ] && [ ! -e "$root/tmp/.hib_post" ] &&
        awk 'NR==1 {header=($1=="Filename")} $1=="/dev/mmcblk0p9" {started=1}
             END {exit !(header && !started)}' "$root/proc/swaps"
}
# The stock program clears the image header before enabling swap. An empty
# swaps list by itself also occurs in that destructive pre-swap interval.
writer_state_valid() {
    status=$("$ctl" --all show hibernation.service -p LoadState -p ActiveState -p SubState -p NeedDaemonReload \
        -p ExecMainStartTimestampMonotonic -p ExecMainPID -p ExecStart) || return 1
    printf '[hibernation.service.%s]\n%s\n' "$1" "$status"
    loaded "$status" || return 1
    started=$(value "$status" ExecMainStartTimestampMonotonic) || return 1
    pid=$(value "$status" ExecMainPID) || return 1
    state=$(value "$status" ActiveState)/$(value "$status" SubState)
    case "$stage" in
        before-capture)
            [ "$started" = 0 ] && [ "$pid" = 0 ] || return 1
            case "$state" in inactive/dead|activating/start-pre) return 0;; *) return 1;; esac
            ;;
        image-header)
            [ "$state" = activating/start ] && [ "$pid" = "${PPID:-}" ] || return 1
            printf '%s\n' "$pid" "$started" | awk 'BEGIN {ok=1} !/^[1-9][0-9]*$/ {ok=0} END {exit !ok}' || return 1
            main=$(value "$status" ExecStart) || return 1
            printf '%s\n' "$main" | awk -F ' ; ' '
                $1=="{ path=/usr/bin/hibernation.sh" && $2=="argv[]=/usr/bin/hibernation.sh" &&
                $3=="ignore_errors=no" && $0!~/}.*\{/ {ok=1}
                END {exit !ok}'
            ;;
    esac
}
printf 'check=NX500_FULL040_PRECAPTURE_2\nstage=%s\n' "$stage"
boot=$(cat "$root/proc/sys/kernel/random/boot_id") || stop BOOT_ID_UNAVAILABLE
printf '%s\n' "$boot" | grep -Eq '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$' || stop INVALID_BOOT_ID
mode=$(cat "$root/sys/power/bootmode") || stop BOOT_MODE_UNAVAILABLE
printf 'boot_id=%s\nbootmode=%s\n' "$boot" "$mode"
case "$mode" in 1|3) ;; *) stop NOT_A_CAPTURE_BOOT;; esac
snapshot_untouched || stop NOT_BEFORE_SNAPSHOT_PREPARATION
link=$root/etc/systemd/system/$event.wants/$companion
[ -L "$link" ] && [ "$(readlink "$link")" = "/etc/systemd/system/$companion" ] || stop ON_DISK_LINK_MISMATCH

# Take the inventory first so that requesting a unit by name cannot repair an
# old saved manager as a side effect and make it look like the capture manager.
inventory=$("$ctl" --all --full --no-pager --no-legend list-units) || stop INVENTORY_UNAVAILABLE
writer_reason=OEM_WRITER_ALREADY_STARTED_OR_UNKNOWN
[ "$stage" != image-header ] || writer_reason=WRITER_IS_NOT_CURRENT_STOCK_MAIN
writer_state_valid before || stop "$writer_reason"
for unit in "$watch" "$event" "$companion"; do
    printf '%s\n' "$inventory" | awk -v name="$unit" '
      {for(i=1;i<NF;i++) if($i==name && $(i+1)=="loaded") ok=1}
      END {exit !ok}' || stop "NOT_IN_CAPTURE_MANAGER_$unit"
done
path_view=$("$ctl" --all show "$watch" -p LoadState -p Triggers -p NeedDaemonReload) || stop PATH_QUERY_FAILED
event_view=$(query_event) || stop EVENT_QUERY_FAILED
child_view=$("$ctl" --all show "$companion" -p LoadState -p After -p ActiveState -p ExecStart -p NeedDaemonReload) || stop COMPANION_QUERY_FAILED
printf '[%s]\n%s\n[%s]\n%s\n[%s]\n%s\n' "$watch" "$path_view" "$event" "$event_view" "$companion" "$child_view"
loaded "$path_view" && loaded "$event_view" && loaded "$child_view" || stop UNITS_STALE_OR_UNLOADED
has_word "$(value "$path_view" Triggers)" "$event" &&
    has_word "$(value "$event_view" TriggeredBy)" "$watch" || stop STOCK_EVENT_DISCONNECTED
has_word "$(value "$event_view" Wants)" "$companion" || stop COMPANION_MISSING_FROM_WANTS
has_word "$(value "$child_view" After)" "$event" || stop WRONG_RESUME_ORDER
[ "$(value "$child_view" ActiveState)" = inactive ] || stop COMPANION_NOT_WAITING_FOR_RESTORE
cmd=$(value "$child_view" ExecStart) || stop HELPER_UNAVAILABLE
printf '%s\n' "$cmd" | awk -F ' ; ' -v argv="argv[]=/bin/sh $helper" '
    $1=="{ path=/bin/sh" && $2==argv && $3=="ignore_errors=no" && $0!~/}.*\{/ {ok=1}
    END {exit !ok}' || stop WRONG_RESUME_HELPER
if [ "$stage" = image-header ]; then
    after_event=$(query_event) || stop EVENT_QUERY_FAILED
    [ "$event_view" = "$after_event" ] || stop EVENT_CHANGED_BEFORE_IMAGE_WRITE
fi
[ "$(cat "$root/proc/sys/kernel/random/boot_id")" = "$boot" ] &&
    [ "$(cat "$root/sys/power/bootmode")" = "$mode" ] && snapshot_untouched || stop CAPTURE_STATE_CHANGED
writer_state_valid after || stop "$writer_reason"
printf 'decision=PASS\nreason=CAPTURE_MANAGER_REFERENCES_INSTALLED_WAKE\n'
