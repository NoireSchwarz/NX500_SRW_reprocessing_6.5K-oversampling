/* SPDX-License-Identifier: AGPL-3.0-only; appended to the original radio journal. */
struct aba_stage{unsigned long long start,end,body0,body1,net0,net1,frames0,frames1;unsigned samples,valid;};
struct aba_data{unsigned enabled,state,saved,dirty,changed,restored,reason,original,bmask,sd_saved,ifindex,reset_cleanup;pid_t host,server;unsigned long long host_tick,server_tick,deadline,phase_start,last_sample,last_body,last_net,trigger_body,trigger_net;struct aba_stage stages[3];};
#define ABA_ACTION_BIT 0x2000u
#define ABA_ARM "/mnt/mmc/nx-wifi-mgmt-aba-rc611/arm"
#define ABA_ARM_TEXT "NXL_WIFI_MGMT_ABA_RC6111\n"
enum aba_reason{ABAR_NONE,ABAR_ARM,ABAR_IDENTITY,ABAR_COUNTER,ABAR_AUTO,ABAR_NO_ACTION,ABAR_ORIGINAL,ABAR_COVERAGE,ABAR_QUERY,ABAR_FOREIGN_MASK,ABAR_RESTORE,ABAR_STOP,ABAR_TIMEOUT};
