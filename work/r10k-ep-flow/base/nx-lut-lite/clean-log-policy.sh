# Sourced by the existing LUT engine while its engine.lock is held.
# The marker reserves the neutral route before preparation, not after Gamma.
CLEAN_LOG=$RUN/clean-log.policy
clean_log_gamma_status() {
    [ -f "$GAMMA" ] && [ ! -L "$GAMMA" ] && [ -x "$GAMMA" ] &&
        [ "$("$OPS" sha256 "$GAMMA")" = "$GAMMA_RELOAD_SHA" ] || return 1
    "$GAMMA" status > "$ST/clean-log-status.txt" 2>&1 || return 1
    grep -qx 'NXL_GAMMA_RELOAD1' "$ST/clean-log-status.txt" &&
        grep -qx 'end=NXL_GAMMA_RELOAD_END1' "$ST/clean-log-status.txt" &&
        grep -qx 'fault=0' "$ST/clean-log-status.txt"
}
clean_log_factory_status() {
    clean_log_gamma_status || return 1
    grep -qx 'mode=FACTORY' "$ST/clean-log-status.txt" &&
        grep -Eq '^phase=(OFF|FACTORY_QUEUED)$' "$ST/clean-log-status.txt"
}
clean_log_allow_look() {
    [ ! -e "$CLEAN_LOG" ] && [ ! -L "$CLEAN_LOG" ] || die 'CLEAN_LOG_ACTIVE_RESTORE_GAMMA_FIRST'
    if [ -e "$RUN/gamma-r11h.lease" ] || [ -L "$RUN/gamma-r11h.lease" ]; then
        clean_log_gamma_status || die 'GAMMA_STATE_UNAVAILABLE_RESTORE_OR_RESTART'
        grep -qx 'mode=LOG' "$ST/clean-log-status.txt" && die 'CLEAN_LOG_ACTIVE_RESTORE_GAMMA_FIRST'
        grep -Eq '^phase=(PREPARED|WAIT_NATIVE|FAILED)$' "$ST/clean-log-status.txt" && die 'GAMMA_TRANSITION_RESTORE_FIRST'
    fi
    return 0
}
clean_log_prepare() {
    [ ! -e "$CLEAN_LOG" ] && [ ! -L "$CLEAN_LOG" ] || die 'CLEAN_LOG_PENDING_RESTORE_FIRST'
    [ -f "$GAMMA" ] && [ ! -L "$GAMMA" ] && [ -x "$GAMMA" ] &&
        [ "$("$OPS" sha256 "$GAMMA")" = "$GAMMA_RELOAD_SHA" ] || die 'GAMMA_COMPONENT_IDENTITY_FAILED'
    # This new host command is read-only: same quiet_source guard as Gamma,
    # with no CAP allocation, RTOS entry, ptrace or parameter submission.
    "$GAMMA" preflight > "$ST/clean-log-preflight.txt" 2>&1 || die 'CLEAN_LOG_CAMERA_NOT_IDLE'
    clean_log_factory_status || die 'CLEAN_LOG_RESTORE_GAMMA_FIRST'
    marker=$CLEAN_LOG.new.$$
    [ ! -e "$marker" ] && [ ! -L "$marker" ] || die 'CLEAN_LOG_MARKER_EXISTS'
    (set -C; printf 'NXL_CLEAN_LOG1 PREPARE\n' > "$marker") || die 'CLEAN_LOG_MARKER_SAVE_FAILED'
    mv "$marker" "$CLEAN_LOG" || die 'CLEAN_LOG_MARKER_PUBLISH_FAILED'
    # Existing production operations, under one engine lock. Original
    # session backups survive; no user look is silently reapplied on exit.
    video_restore || die 'CLEAN_LOG_VIDEO_RESTORE_FAILED'
    # Private, package-owned identity table; independent of removable look
    # names and strength presets. It cannot bake a creative look into Log.
    clean_log_saved_lab=$LAB
    LAB=$D/data/clean-log
    apply_table NXLOG_REF_V1 100
    LAB=$clean_log_saved_lab
    video_enable
    printf 'NXL_CLEAN_LOG1 READY\n' > "$marker" || die 'CLEAN_LOG_READY_SAVE_FAILED'
    mv "$marker" "$CLEAN_LOG" || die 'CLEAN_LOG_READY_PUBLISH_FAILED'
    echo 'OK CLEAN_LOG_NEUTRAL_ROUTE_READY'
}
clean_log_release() {
    [ -e "$CLEAN_LOG" ] || [ -L "$CLEAN_LOG" ] || return 0
    [ -f "$CLEAN_LOG" ] && [ ! -L "$CLEAN_LOG" ] || die 'CLEAN_LOG_MARKER_INVALID'
    # Never replace the neutral table while any Log transaction is active.
    clean_log_factory_status || die 'CLEAN_LOG_GAMMA_RESTORE_NOT_CONFIRMED'
    "$GAMMA" preflight > "$ST/clean-log-release-preflight.txt" 2>&1 || die 'CLEAN_LOG_CAMERA_NOT_IDLE_FOR_LOOK_RESTORE'
    restore || die 'CLEAN_LOG_ORIGINAL_LOOK_RESTORE_FAILED'
    rm -f "$CLEAN_LOG" || die 'CLEAN_LOG_MARKER_REMOVE_FAILED'
    echo 'OK CLEAN_LOG_RELEASED_FACTORY_LOOKS_AVAILABLE'
}
