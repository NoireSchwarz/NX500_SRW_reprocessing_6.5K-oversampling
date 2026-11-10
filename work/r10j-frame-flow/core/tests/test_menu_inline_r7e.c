/* SPDX-License-Identifier: AGPL-3.0-only. Actual menu lifecycle, modeled OS wait. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#if defined(_WIN32)
typedef int pid_t;
#define WNOHANG 1
#define WIFEXITED(s) (((s)&127)==0)
#define WEXITSTATUS(s) (((s)>>8)&255)
#else
#include <sys/wait.h>
#endif
#include "../../base/nx-lut-lite/src/ui_state.h"
static int menu_lock_fd=17,rearm_inline,left_panel=1,srw_row=8,action,quitting;
static int video_enabled,apply_strength;
static char demo_look[81];
static pid_t rearm_child;
static struct nxl_help_state help;
static struct nxl_raw_status raw_status;
static char notice[120],order[20];
static unsigned starts,refreshes,progresses,events;
static int start_fail,wait_result,wait_status,wait_error;
static unsigned long ticks(void){return 23;}
static int memory_background(int fd,int mode){assert(fd==17&&mode==4&&rearm_inline);starts++;if(start_fail)return 1;rearm_child=42;return 0;}
static pid_t model_waitpid(pid_t pid,int *status,int flags){assert(pid==42&&flags==WNOHANG);*status=wait_status;errno=wait_error;return wait_result;}
#define waitpid model_waitpid
static void load_raw_status(void){assert(!rearm_child);refreshes++;strcat(order,"S");raw_status.valid=1;raw_status.state=NXL_RAW_NEED_STILL_INIT;strcpy(raw_status.reason,"EMPTY_SESSION_RESOURCE_PROOF_VERIFIED");}
static void load_progress_status(void){progresses++;strcat(order,"P");}
static void launch_event(const char *event,const char *detail,int rc,int error){assert(!strcmp(event,"INLINE_RESET_REFRESHED")&&detail==raw_status.reason&&!error);(void)rc;events++;strcat(order,"E");}
#include "../../base/nx-lut-lite/src/menu_rearm.inc"
#include "fixtures/menu_srw_confirm_r7e.inc"
static void reset(void){rearm_child=0;starts=refreshes=progresses=events=0;order[0]=notice[0]=0;start_fail=wait_error=wait_status=wait_result=0;srw_row=8;left_panel=1;}
int main(void){unsigned checks=0;
 reset();assert(!poll_menu_rearm());checks++;
 start_menu_rearm();assert(starts==1&&rearm_child==42&&!rearm_inline&&!action&&!quitting);checks++;
 start_menu_rearm();assert(starts==1);checks++;
 assert(!poll_menu_rearm()&&!refreshes);checks++;
 wait_result=-1;wait_error=EINTR;assert(!poll_menu_rearm()&&rearm_child==42);checks++;
 wait_result=42;wait_error=0;assert(poll_menu_rearm());assert(!rearm_child&&refreshes==1&&progresses==1&&events==1&&!strcmp(order,"SPE")&&srw_row==3&&strstr(notice,"CHECK OK")&&!quitting&&!action);checks++;
 assert(!poll_menu_rearm()&&refreshes==1);checks++;
 reset();start_fail=1;start_menu_rearm();assert(!rearm_child&&!rearm_inline&&strstr(notice,"COULD NOT START")&&!quitting);checks++;
 for(unsigned i=0;i<3;i++){
  reset();start_menu_rearm();wait_result=i==2?-1:42;wait_status=i==0?1<<8:15;wait_error=i==2?ECHILD:0;
  assert(poll_menu_rearm()&&!rearm_child&&refreshes==1&&progresses==1&&strstr(notice,"FINISHED")&&!quitting&&!action);checks++;
 }
 reset();left_panel=0;start_menu_rearm();wait_result=42;assert(poll_menu_rearm()&&srw_row==8&&!left_panel);checks++;
 for(unsigned i=0;i<2;i++){
  reset();raw_status.valid=1;raw_status.state=NXL_RAW_RESTART_REQUIRED;raw_status.front_verified=0;
  strcpy(raw_status.resume,"front");strcpy(raw_status.reason,"REAL_KERNEL_RESTART_NOT_VERIFIED");
  srw_row=i?4:8;srw_confirm();assert(starts==1&&rearm_child==42&&!quitting&&!action);checks++;
  srw_confirm();assert(starts==1&&!quitting&&!action);checks++;
 }
 reset();raw_status.valid=1;raw_status.state=NXL_RAW_RESTART_REQUIRED;strcpy(raw_status.resume,"front");strcpy(raw_status.reason,"FRONT_FAILED_NATIVE_STATE_UNVERIFIED");
 assert(nxl_raw_can_rearm(&raw_status));checks++;
 strcpy(raw_status.reason,"FRONT_NEEDS_STILL_INIT");assert(strstr(nxl_raw_status_hint(&raw_status),"NO TASK STARTED"));checks++;
 printf("PASS %u actual menu lifecycle cases: asynchronous wait, full status/progress reload, success/error/signal refresh, no forced close\n",checks);return 0;
}
