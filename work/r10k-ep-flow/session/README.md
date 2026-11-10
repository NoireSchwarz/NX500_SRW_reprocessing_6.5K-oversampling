# Durable RAW/JPEG session controller — install-r4

Protocol prefix remains 0.4.0-demo51-422. `--version` additionally contains `STATE_JOURNAL_R4` and `FRESH_SRW_EVERY_JOB_R4`. This component manages state only; it never allocates/frees CAP, deletes hardware locks or submits ISP/JPEG tasks.

## State publication

Each JOB has an append-only `STATE.journal`. Every record is the complete existing `NX51STATE1` record, including SHA256 and terminator. Complete records must advance exactly one sequence number; partial, corrupt, duplicated or out-of-order records fail closed. Maximum size is4MiB/4096 records and each record is bounded8192bytes. Publication appends, file/directory fsyncs and re-reads the authoritative sequence before returning success. In particular `begin` cannot authorize a native task before its journal record is verified.

`SESSION.state` and `SUMMARY.txt` are human mirrors. Publication verifies replacement contents and falls back to updating the same inode when replacement is ineffective. Mirror failure is diagnosed and does not replace journal authority. No mirror rollback is used to recover a bad journal tail.

The root `CURRENT.journal` is an append-only, checksummed generation ledger selecting the current JOB. `current.job` is its compatibility mirror. A torn current ledger cannot fall back to an old pointer.

A legacy JOB without a journal still accepts a valid old `SESSION.state`, but checks event sequence consistency. If event sequence exceeds canonical state sequence, it exposes `RESTART_REQUIRED / LEGACY_STATE_DIVERGENCE` at the greatest observed sequence. Event text can strengthen a hold; it cannot prove image completion or authorize clearing a hardware hold. Normal run/new cannot silently bypass this contradiction. Any legacy verified-front reference without a journal is detached while preserving an unknown/reset hold (`LEGACY_FRONT_REQUIRES_FRESH` unless an existing unknown reason is already present). Even later proven-reset reconciliation must request a new SRW front, never migrate old YUV into an R4 journal.

## Fresh full-chain policy

`new` always creates a fresh SRW job with `front=-` and `front_verified=0`. It does not inherit old YUV. A remaining reset requirement is retained. `import-front` is disabled. Within one job, `front-ok` still hands that job's newly completed YUV to its JPEG stage.

## Proof-only preparation for the known false front hold

`nxl-session.arm ROOT prepare-full SCRIPT --prepare-full-worker` holds the same exclusive run lock and live PID/start-tick/token lease as normal supervision. Production permits only `/opt/usr/nx-lut-lite/memory-test.sh`; the installed, identity-pinned worker performs the actual read-only native proof validation and exact matching hold inspection. This is a trusted worker interface, not an operating-system sandbox.

The child receives `NX51_PREPARE_FULL_ONLY=1` and a lease whose mode is `full`. Exporting that environment variable from an ordinary worker cannot forge the lease mode. All normal mutators are refused in this mode except `fail`.

Only `fresh-front ABS_REPORT_DIR` is additionally allowed. It requires the live full lease, RESTART_REQUIRED with the specific `FRONT_RESTART_OR_PATH_INVALID` or `LEGACY_STATE_DIVERGENCE` reason, no backend attempt/stage, and a same-job real `front_N/front` report path. The worker must already have validated the full front/parameter/cleanup proof with the fixed POST verifier and confirmed no other hardware activity or JPEG attempt. The C manager does not infer this proof from file existence.

That explicit proof handoff commits `NEED_STILL_INIT`, `front=-`, `front_verified=0`, `requires_reset=0`, raw422/100, and `FULL_TEST_PREPARED_NO_REUSE`. It preserves the job, old files and front attempt floor; the next normal begin creates a new front attempt and reruns SRW decoding/ISP. The report argument is audit evidence, not a YUV source for encoding. The C manager clears no /tmp lock and releases no CAP. Worker failure after this handoff re-holds the state. Success without the explicit handoff is not considered success.

## Existing supervision and reset observation

One process-owned CLOEXEC run flock spans each worker. State mutation requires its live lease. Crashed/running jobs remain unknown. The exact OEM alias `/mnt/mmc -> /opt/storage/sdcard` is accepted; other parent/report/state links remain refused.

A Linux boot ID remains optional for fresh jobs. Only two reliable observed IDs that differ can prove a reset; unknown-to-known and known-to-unknown do not release any reset requirement. Normal power cycling is not presumed to change the ID. The specific proof-only preparation above is a separate explicit validation path, not a boot-number override.

## Diagnostics

Diagnostics go to stderr and append to `/mnt/mmc/NXDEMO51/logs/session.log` only if that dedicated folder already exists. The manager does not create a log folder during readonly status and does not write compatibility logs in SD root. Stdout remains the exact machine-readable status protocol. Readonly status acquires a shared lock only on an already existing state.lock; it never creates a lock, pointer or job. This keeps it away from an incomplete append while an exclusive writer is active.

## Build and verification

Build with session/tools/build_session.py using the existing Zig0.13/musl ARM toolchain. Deploy only session/bin/nxl-session.arm, never HOST_MODEL or filesystem-test binaries.

Tests cover53 host cases including real symlink topology, stale state/current mirrors, torn journals, legacy contradiction, proof-mode authorization, fresh-job policy and failure re-holding. Actual ARM bootstrap tests execute production status with no proc boot ID and a non-HOST_MODEL filesystem variant that differs only in three fixed path constants; chroot is denied by the workspace sandbox. No test submits camera hardware jobs. Validation records bind both session.c and journal.inc.

R7 adds an explicit `rearm /opt/usr/nx-lut-lite/rearm-session.sh --rearm-worker` supervisor for only the untouched seq1 `REAL_KERNEL_RESTART_NOT_VERIFIED` job inherited from the immediate prior completed job in the checksummed CURRENT ledger. Previous STATE.journal is authoritative; stale SESSION mirrors are not promoted. Dedicated mode=rearm workers can only call `rearm-fresh` or fail conservatively. A fresh token-bound proof must independently verify both old front/backend allocation extents and backend resource retirement, known references, code, parked shell, empty queue and worker idleness. Historical COMPLETE is provenance, not cleanup evidence. On proof failure the held journal remains unchanged; no allocation or unknown lock is released. Accepted proof only permits a new Still initialization and fresh SRW pass; it does not restore quantization SRAM or reuse old YUV. Linux/ARM tests of positive receipts model the trusted native proof contract and do not establish that a camera meets it.
