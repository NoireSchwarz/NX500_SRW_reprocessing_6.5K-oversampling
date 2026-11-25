# Runs through the existing, owner-configured SD mount callback.
# The PC places this fixed text in an in-memory shell command. No additional
# program is read from the card. The system and capture image are left alone.
root=
PATH=/bin:/usr/bin
export PATH
umask 077
sd=$root/mnt/mmc
trial=$sd/nxl-full040-simple-wake/trial-@RUN@
state=$root/opt/usr/nxl-full040
run=$root/tmp/nx-lut-lite
helper=$state/resume-after-stock.sh
openssl=$root/usr/bin/openssl
lock=$root/tmp/nxl-full040-card-dispatch.lock
receipt=

one_line() {
    value=
    [ -r "$1" ] && IFS= read -r value < "$1"
    printf '%s' "$value"
}
uptime() { value=$(one_line "$root/proc/uptime"); printf '%s' "${value%% *}"; }
usb() { one_line "$root/sys/class/extcon/USB/state"; }
mode() { one_line "$root/sys/power/bootmode"; }
post() { [ -f "$root/tmp/.hib_post" ] && printf 1 || printf 0; }
card_up() {
    [ "$(usb)" = 0 ] && awk '
        $1=="/dev/mmcblk1p1" && ($2=="/opt/storage/sdcard" || $2=="/mnt/mmc") &&
        $3=="exfat" && $4 ~ /(^|,)rw(,|$)/ {ok=1} END {exit !ok}' "$root/proc/mounts"
}
normal() {
    case "$(mode)" in 2|3) ;; *) return 1;; esac
    [ "$(post)" = 1 ] && [ ! -e "$root/opt/etc/.hib_capturing" ] &&
        awk 'NR==1 {ok=($1=="Filename")} $1=="/dev/mmcblk0p9" {used=1} END {exit !(ok && !used)}' "$root/proc/swaps"
}
pin() {
    [ -f "$1" ] && [ ! -L "$1" ] || return 1
    sum=$("$openssl" dgst -sha256 < "$1" 2>/dev/null) || return 1
    [ "${sum##* }" = "$2" ]
}
readiness() {
    eye=$(one_line "$run/eye-af.resume-ready")
    listener=$(one_line "$run/listener.ready")
    printf 'eye_ready_pid=%s\nlistener_pid=%s\n' "$eye" "$listener"
    case "$listener" in ''|*[!0-9]*|0) ;; *)
        printf 'listener_exe=%s\n' "$(readlink "$root/proc/$listener/exe" 2>/dev/null)";; esac
    [ ! -r "$run/eye-af.status" ] || awk 'NR==1 {for(i=1;i<=NF;i++) if($i ~ /^(eye_frames|state|schema|pid)=/) print $i}' "$run/eye-af.status"
}
helper_status() {
    [ -f "$state/normal-wake.status" ] && [ ! -L "$state/normal-wake.status" ] || return 0
    awk -F= '$1=="result" || $1=="uptime" {print "installed_" $0}' "$state/normal-wake.status"
}
claim_receipt() {
    [ -z "$receipt" ] && [ "$slots_full" = 0 ] && card_up &&
        [ ! -L "$sd/nxl-full040-simple-wake" ] && [ -d "$trial" ] && [ ! -L "$trial" ] || return
    for slot in 1 2 3; do
        if mkdir "$trial/call-$slot" 2>/dev/null; then
            receipt=$trial/call-$slot
            (set -C; exec > "$receipt/entry.txt" || exit 1
             printf 'entry=sd_mount\ntrial=@RUN@\nboot_id=%s\nentry_uptime=%s\nentry_usb=%s\nentry_mode=%s\nentry_hib_post=%s\n' \
                "$boot" "$first_time" "$first_usb" "$first_mode" "$first_post"
             printf 'entry_epoch=%s\n' "$first_epoch"
             readiness)
            return
        fi
    done
    slots_full=1
}
note() {
    [ -n "$receipt" ] && card_up && [ -d "$receipt" ] && [ ! -L "$receipt" ] || return 0
    (set -C; exec > "$receipt/outcome.txt" || exit 1
     printf 'dispatch=%s\nhelper_exit=%s\nuptime=%s\n' "$1" "$2" "$(uptime)"
     printf 'outcome_epoch=%s\n' "$(date +%s)"
     if [ "$1" = INSTALLED_COMBINED_HELPER ]; then
         after=$(helper_status)
         [ "$after" != "$before" ] && changed=1 || changed=0
         printf 'installed_status_changed=%s\n' "$changed"
         [ -z "$after" ] || printf '%s\n' "$after"
     fi
     readiness)
}
dispatch() {
    boot=$(one_line "$root/proc/sys/kernel/random/boot_id")
    first_epoch=$(date +%s)
    first_time=$(uptime); first_usb=$(usb); first_mode=$(mode); first_post=$(post)
    slots_full=0
    mkdir "$lock" 2>/dev/null || return
    trap 'rmdir "$lock" 2>/dev/null' 0
    step=0
    while :; do
        claim_receipt
        case "$(mode)" in ''|2|3) ;; *) note NON_RESTORE_BOOT -; return;; esac
        if [ -e "$root/opt/etc/.hib_capturing" ]; then note CAPTURE_IN_PROGRESS -; return; fi
        if card_up && normal; then break; fi
        if [ "$step" -ge 30 ]; then note NORMAL_MODE_TIMEOUT -; return; fi
        sleep 1
        step=$((step+1))
    done
    if pin "$helper" f014691f33bae4c1e84fbf8e3f90d48007d40d426503e3e30c4dba96ce13da21; then
        # This existing helper validates the loaded generation and handles
        # both eye readiness and the menu's input listener idempotently.
        if card_up && normal; then
            before=$(helper_status)
            /bin/sh "$helper"
            note INSTALLED_COMBINED_HELPER "$?"
        else
            note NORMAL_STATE_CHANGED -
        fi
    else
        note INSTALLED_HELPER_UNCONFIRMED -
    fi
}
trap '' HUP
(dispatch) </dev/null >/dev/null 2>&1 &
exit 0
