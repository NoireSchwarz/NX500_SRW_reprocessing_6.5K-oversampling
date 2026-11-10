# Included by memory-test.sh after authentication and component checks.
# Keep this in the signed parent script at packaging time.
capture_srw_lut(){
    if regular "$RUN/session/ready" && regular "$RUN/session/expected.bin";then
        cp "$RUN/session/expected.bin" "$report/srw-lut.bin" && "$OPS" same "$RUN/session/expected.bin" "$report/srw-lut.bin" || fail LUT_SNAPSHOT_FAILED
        printf 'table=applied_live_LUT_snapshot\n' > "$report/srw-lut.origin" || fail LUT_SNAPSHOT_FAILED
    else
        # Native frontend generates the canonical identity slot if no table
        # was applied. The native LUT stage is still executed and audited.
        printf 'table=identity_no_applied_LUT\n' > "$report/srw-lut.origin" || fail LUT_SNAPSHOT_FAILED
    fi
}
wait_shared_front(){
    rc=1;front_wait=0
    while [ "$front_wait" -lt 420 ];do
        if regular "$front_owner_report/workspace-owner.ready" && regular "$front_owner_report/shared-workspace.bin" &&
           [ "$(hash "$front_owner_report/workspace-owner.ready")" = "$(hash "$front_owner_report/shared-workspace.bin")" ] && kill -0 "$front_owner" 2>/dev/null;then
            shared_front_ready=1;rc=0;return 0
        fi
        owner_state=
        { IFS=' ' read -r owner_stat_pid owner_stat_name owner_state owner_stat_rest < "/proc/$front_owner/stat"; } 2>/dev/null || :
        if ! kill -0 "$front_owner" 2>/dev/null || [ "$owner_state" = Z ] || [ "$owner_state" = X ];then
            rc=0;wait "$front_owner" || rc=$?;[ "$rc" != 0 ] || rc=1;front_owner=;return 1
        fi
        front_wait=$((front_wait+1));sleep 1
    done
    reason=FRONT_OWNER_READY_TIMEOUT
    return 1
}
release_shared_front(){
    [ -n "$front_owner" ] || return 0
    if [ "$shared_front_ready" = 1 ] && is_nonce "$front_owner_nonce" && absent "$front_owner_report/WORKSPACE_OWNER_RELEASE";then
        printf 'NXSWRELEASE1 %s\n' "$front_owner_nonce" > "$front_owner_report/WORKSPACE_OWNER_RELEASE" || return 1
    else
        kill -TERM "$front_owner" 2>/dev/null || :
    fi
    owner_rc=0;wait "$front_owner" || owner_rc=$?;front_owner=
    [ "$owner_rc" = 0 ] || [ "$shared_front_ready" != 1 ]
}
