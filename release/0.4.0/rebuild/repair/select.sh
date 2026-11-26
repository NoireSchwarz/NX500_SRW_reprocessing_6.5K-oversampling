#!/bin/sh
# Select only the generation verified by the image writer or the restored process.
set -u
root=
rt=$root/usr/local/lib/@GENERATION@
state=$root/opt/usr/@GENERATION@
action=@ACTION@
regular() { [ -f "$1" ] && [ ! -L "$1" ]; }
boot=$(cat "$root/proc/sys/kernel/random/boot_id" 2>/dev/null)
if [ "$action" = launch ] && [ -n "$boot" ] && [ -n "${HIB:-}" ] && regular "$rt/capture-origin"; then
    report=$(sed -n '3p' "$rt/capture-origin")
    for proof in "$root"/run/nxl-full040-capture/image-header.*.txt; do
        if regular "$proof" && grep -Fqx "boot_id=$boot" "$proof" &&
            grep -Fqx "capture_report=$report" "$proof" &&
            grep -Fqx 'eye_build=@GENERATION@' "$proof" && grep -Fqx 'decision=PASS' "$proof"; then
            exec /bin/sh "$rt/launch.sh" "$@"
        fi
    done
fi
if [ "$action" = wake ] && [ -n "$boot" ] && regular "$state/launch-claim" &&
    grep -Fqx "boot_id=$boot" "$state/launch-claim" &&
    grep -Fqx 'file=/usr/local/lib/@GENERATION@/nxl-eye-af.so' "$state/launch-claim"; then
    exec /bin/sh "$rt/wake.sh" "$@"
fi
if regular "$rt/legacy-$action.sh"; then
    exec /bin/sh "$rt/legacy-$action.sh" "$@"
fi
if [ "$action" = launch ]; then
    unset HIB
    exec "$root/usr/apps/com.samsung.di-camera-app/bin/di-camera-app" "$@"
fi
exit 0
