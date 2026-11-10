/* Independent transaction scenarios for the OEM recovery proof contract.
 * No ISP, native command, register write, CAP allocation or POSIX wait. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/oem_restore_contract.h"

static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static struct oem_snapshot stopped(void) {
    struct oem_snapshot s = {1, 0, 0x91004000, 0x82004000,
                             0x805808d0, 1, 0, 1, 1};
    return s;
}
static struct oem_finish finished(void) {
    struct oem_finish f = {1, 1, 1, 1, 1, 0, 1, 3};
    return f;
}

int main(int argc,char **argv) {
    struct oem_snapshot initial = stopped(), s;
    struct oem_finish f = finished();
    CHECK(oem_preflight(&initial));

    /* An originally stopped UI is a different transaction: bypassing its
     * real wrapper would start RTOS while leaving the UI flag stopped. */
    s = initial; s.ui_running = 0; CHECK(!oem_preflight(&s));
    s = initial; s.ui_running = 2; CHECK(!oem_preflight(&s));
    s = initial; s.expected = 8; CHECK(!oem_preflight(&s));
    s = initial; s.expected = 9; CHECK(!oem_preflight(&s));
    s = initial; s.handle = 0; CHECK(!oem_preflight(&s));
    s = initial; s.processor = 0x81000001; CHECK(!oem_preflight(&s));
    s = initial; s.processor = 0x80fffffc; CHECK(!oem_preflight(&s));
    s = initial; s.processor = 0x93fff004; CHECK(!oem_preflight(&s));
    s = initial; s.vtable = 0x80592640; CHECK(!oem_preflight(&s));
    s = initial; s.state = 2; CHECK(!oem_preflight(&s));
    s = initial; s.request = 1; CHECK(!oem_preflight(&s));
    s = initial; s.still_idle = 0; CHECK(!oem_preflight(&s));
    s = initial; s.dma_idle = 0; CHECK(!oem_preflight(&s));

    /* Real asynchronous progression: acceptance/early state transition is
     * distinct from return, detach, fresh completion and stable live state. */
    s = initial; CHECK(!oem_completion(&s, &initial, &f));
    s.state = 2; CHECK(!oem_completion(&s, &initial, &f));
    s.request = 1; f.api_inflight = 1;
    CHECK(!oem_completion(&s, &initial, &f));
    f.api_inflight = 0; f.api_return_verified = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    f.api_return_verified = 1; f.detached = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    f.detached = 1; f.fresh_log = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    f.fresh_log = 1; f.stable = 2;
    CHECK(!oem_completion(&s, &initial, &f));
    f.stable = 3; CHECK(oem_completion(&s, &initial, &f));

    /* Physical/live-looking output cannot excuse incomplete restoration or
     * multiple submissions after a timeout. */
    f.stack_restored = 0; CHECK(!oem_completion(&s, &initial, &f));
    f = finished(); f.code_restored = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    f = finished(); f.submitted = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    f.submitted = 2; CHECK(!oem_completion(&s, &initial, &f));
    f = finished(); f.api_inflight = 1;
    CHECK(!oem_completion(&s, &initial, &f));

    /* A UI stop, callback waiter, replacement camera handle or replacement
     * liveview object makes even a fresh-looking completion unusable. */
    f = finished(); s = initial; s.state = 2; s.request = 1;
    s.ui_running = 0; CHECK(!oem_completion(&s, &initial, &f));
    s.ui_running = 1; s.expected = 9;
    CHECK(!oem_completion(&s, &initial, &f));
    s.expected = 0; s.handle += 4;
    CHECK(!oem_completion(&s, &initial, &f));
    s.handle = initial.handle; s.processor += 4;
    CHECK(!oem_completion(&s, &initial, &f));
    s.processor = initial.processor; s.vtable = 0;
    CHECK(!oem_completion(&s, &initial, &f));
    s.vtable = initial.vtable; s.state = 3;
    CHECK(!oem_runtime_consistent(&s, &initial));
    s.state = 2; s.request = 2;
    CHECK(!oem_runtime_consistent(&s, &initial));

    CHECK(!oem_fresh_completion("[Live] START_CALLBACK 0x0\n"));
    CHECK(!oem_fresh_completion("[Live] --------->LIVEVIEW_START_CALLBACK 0x0\n"));
    CHECK(!oem_fresh_completion("Still Status = RUN\nLiveview Status = RUN\n"));
    CHECK(!oem_fresh_completion("LIVEVIEW_STOP_COMPLETION(0x0000c104)\n"));
    CHECK(oem_fresh_completion("[Live] LIVEVIEW_START_COMPLETION(0x0000c102)\n"));
    CHECK(oem_fresh_completion("[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION\n"));
    CHECK(!oem_fresh_completion("[ResLiveview] LIVEVIEW_START_COMPLETION\n"));
    CHECK(!oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)"));
    CHECK(oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)\n"));
    CHECK(!oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)\n"
                               "LIVEVIEW_STOP_COMPLETION(0x0000c104)\n"));
    CHECK(!oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)\n"
                               "[Live] [state] E_LV_STATE_NORM_LIVE >> E_LV_STATE_IDLE\n"));
    CHECK(!oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)\n"
                               "LIVEVIEW_START_COMPLETION(0x0000c102)\n"));
    CHECK(!oem_fresh_completion("[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION\n"
                               "[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION\n"));
    CHECK(oem_fresh_completion("LIVEVIEW_START_COMPLETION(0x0000c102)\n"
                              "[CAP] SEQ << LIVE: CAPTURE_CALLBACK_ID_LIVEVIEW_START_COMPLETION\n"));

    /* Use the actual successful machine report. Its completed render keeps
     * configured format/routing registers: treating the all-zero stop-time
     * template as the only safe state would reject this real input. */
    CHECK(argc==2);
    FILE *fixture=fopen(argv[1],"rb");CHECK(fixture!=NULL);
    uint32_t handoff[64];CHECK(fread(handoff,4,64,fixture)==64);
    CHECK(fgetc(fixture)==EOF);CHECK(fclose(fixture)==0);
    CHECK(!memcmp(handoff+8,handoff+22,14*4));
    CHECK(handoff[8+2]==0x0040a001 && handoff[8+4]==0x04845ef9 &&
          handoff[8+8]==0xf0f7);
    CHECK(oem_dma_idle(handoff+8));
    const unsigned unsafe[] = {0,1,3,5,6,7,9,10,11,12,13};
    for(unsigned i=0;i<sizeof unsafe/sizeof *unsafe;i++) {
        uint32_t d[14];memcpy(d,handoff+8,sizeof d);d[unsafe[i]]=1;
        CHECK(!oem_dma_idle(d));
    }

    printf("PASS %u independent OEM recovery transaction checks; no hardware\n", checks);
    return 0;
}
