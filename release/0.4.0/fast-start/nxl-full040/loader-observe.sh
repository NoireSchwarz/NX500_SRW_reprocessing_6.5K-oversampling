#!/bin/sh
# Check the real loader using a short-lived stock process. No camera AF calls.
set -u
task_root=
mapped=$1
claim=$2
case "$mapped" in "$task_root/usr/local/lib/nxl-full040/nxl-eye-af.so") ;; *) exit 64;; esac
[ -f "$claim" ] && [ ! -L "$claim" ] && grep -Fqx "file=$mapped" "$claim" || exit 2
dev=$(sed -n 's/^mapped_dev=//p' "$claim")
ino=$(sed -n 's/^mapped_ino=//p' "$claim")
case "$dev" in ''|*[!0-9a-fA-F:]*) exit 2;; esac
case "$ino" in ''|0|*[!0-9]*) exit 2;; esac
exe=$(readlink -f "$task_root/bin/sleep") && [ -n "$exe" ] || exit 3
printf 'stock_exe=%s\nfile=%s\nexpected_dev=%s expected_inode=%s\n' "$exe" "$mapped" "$dev" "$ino"
LD_BIND_NOW=1 LD_PRELOAD="$mapped" "$task_root/bin/sleep" 4 </dev/null &
child=$!
printf 'child_pid=%s\n' "$child"
seen=0
matched=0
i=0
while [ "$i" -lt 4 ]; do
    proc=$task_root/proc/$child
    if [ "$(readlink "$proc/exe" 2>/dev/null)" = "$exe" ] && [ -r "$proc/maps" ]; then
        seen=1
        if awk -v path="$mapped" -v dev="$dev" -v ino="$ino" '
            $6==path && $2~/x/ {
                print "executable_map=" $0
                if (NF==6 && $4==dev && $5==ino) matched=1
            }
            END {exit !matched}' "$proc/maps"; then matched=1; break; fi
    fi
    kill -0 "$child" 2>/dev/null || break
    i=$((i+1))
    sleep 1
done
wait "$child"
rc=$?
printf 'stock_child_exit=%s\nstock_exe_seen=%s\nroot_file_mapped=%s\n' "$rc" "$seen" "$matched"
[ "$rc" = 0 ] && [ "$matched" = 1 ]
