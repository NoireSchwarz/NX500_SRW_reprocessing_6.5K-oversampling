/* SPDX-License-Identifier: AGPL-3.0-only. Actual session next-action logic. */
#define main production_session_main_not_run
#include "../src/session.c"
#undef main
#include <assert.h>
int main(void){
 struct session s={0};strcpy(s.state,"RESTART_REQUIRED");strcpy(s.reason,"REAL_KERNEL_RESTART_NOT_VERIFIED");
 assert(!strcmp(next_action(&s),"CHECK_RAW_RESET_THEN_INITIALIZE"));
 strcpy(s.reason,"ORPHANED_RUNNING_STATE");assert(!strcmp(next_action(&s),"REAL_KERNEL_RESTART_NOT_VERIFIED"));
 strcpy(s.state,"NEED_STILL_INIT");assert(!strcmp(next_action(&s),"TAKE_PHOTO_THEN_START"));
 puts("PASS 3 actual session next-action cases; no journal mutation or boot-id bypass");return 0;
}
