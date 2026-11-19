from context import *
p=T/'base/nx-lut-lite/auto-init.sh'
patch(p,'  selected_status=$root/session-prepared.txt','''  grep -qx state=NEED_STILL_INIT "$root/session-prepared.txt" && grep -qx requires_reset=0 "$root/session-prepared.txt" || fail PREPARED_SESSION_NOT_READY
  selected_status=$root/session-prepared.txt''')
patch(p,'else /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED;fi','''else
  /bin/sh "$RUNNER" --new > "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_FAILED
  grep -qx state=NEED_STILL_INIT "$root/session-selected-photo.txt" && grep -qx requires_reset=0 "$root/session-selected-photo.txt" || fail SELECTED_PHOTO_NEW_SESSION_NOT_READY
 fi''')
patch(p,"sync\nnxl_progress AUTO_READY READY",'''# Compact only this successful operation's optional wrapper logs. Native
# resource records remain available to the next button's rearm verifier.
completed_job=$(sed -n 's/^job_dir=//p' "$root/session-after.txt")
/bin/sh "$LOGPOLICY" complete "$completed_job" "$root" "$token" || printf 'OPTIONAL_LOG_COMPACTION_UNCONFIRMED\\n' >&2
sync
nxl_progress AUTO_READY READY''')
print('PASS fresh session readiness gates and preserved bounded-log policy',flush=True)
