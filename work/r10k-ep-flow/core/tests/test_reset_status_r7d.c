/* SPDX-License-Identifier: AGPL-3.0-only. Test actual menu status helpers. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../base/nx-lut-lite/src/ui_state.h"
int main(void){
 struct nxl_raw_status s={0};s.valid=1;s.state=NXL_RAW_RESTART_REQUIRED;
 strcpy(s.resume,"front");strcpy(s.reason,"REAL_KERNEL_RESTART_NOT_VERIFIED");
 struct nxl_raw_status before=s;
 assert(!strcmp(nxl_raw_status_state_name(&s),"RESET CHECK REQUIRED"));
 assert(!strcmp(nxl_raw_status_action(&s),"VERIFY RESET"));
 assert(strstr(nxl_raw_status_hint(&s),"TAKE A PHOTO"));
 assert(!memcmp(&before,&s,sizeof s));
 s.front_verified=1;assert(!strcmp(nxl_raw_status_state_name(&s),nxl_raw_state_name(s.state)));
 s=before;strcpy(s.reason,"ORPHANED_RUNNING_STATE");assert(!strcmp(nxl_raw_status_state_name(&s),nxl_raw_state_name(s.state)));
 s=before;s.state=NXL_RAW_NEED_STILL_INIT;assert(!strcmp(nxl_raw_status_state_name(&s),nxl_raw_state_name(s.state)));
 s=before;s.valid=0;assert(!strcmp(nxl_raw_status_state_name(&s),nxl_raw_state_name(s.state)));
 puts("PASS 8 actual menu status cases; actionable reset-check label, initialization hint, unchanged stored state and unknown holds");return 0;
}
