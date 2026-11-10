/* SPDX-License-Identifier: AGPL-3.0-only. Session setup stays in the same UI. */
static void start_menu_rearm(void){
 if(rearm_child>0)return;
 rearm_is_new=0;
 rearm_inline=1;int failed=memory_background(menu_lock_fd,4);rearm_inline=0;
 strcpy(notice,failed?"RESET CHECK COULD NOT START - SEE LOGS":"RESET CHECK RUNNING - PLEASE WAIT");
 nxl_help_reset(&help,ticks());
}
static void start_menu_new_session(void){
 if(rearm_child>0)return;
 /* Refresh before choosing a transaction. A completed round still needs a
  * current resource proof; legacy `new` would inherit its requires_reset bit
  * and create an empty cold-held job before that proof could be requested. */
 load_raw_status();
 if(!raw_status.valid)strcpy(notice,"SESSION STATUS UNAVAILABLE");
 else if(raw_status.state==NXL_RAW_FRONT_RUNNING||raw_status.state==NXL_RAW_BACKEND_RUNNING)
  strcpy(notice,"SESSION RUNNING - PLEASE WAIT");
 else if(!strcmp(raw_status.reason,"FRONT_REINSTALL_REQUIRED"))
  strcpy(notice,"REINSTALL FULL PACKAGE FOR NEW FRONT");
 else{
  int operation=raw_status.state==NXL_RAW_COMPLETE||nxl_raw_can_rearm(&raw_status)?4:1;
  rearm_is_new=1;rearm_inline=1;
  int failed=memory_background(menu_lock_fd,operation);rearm_inline=0;
  if(failed)rearm_is_new=0;
  strcpy(notice,failed?"NEW SESSION COULD NOT START - SEE LOGS":"NEW SESSION - CHECKING CURRENT RESOURCES");
 }
 nxl_help_reset(&help,ticks());
}
static int poll_menu_rearm(void){
 if(rearm_child<=0)return 0;
 int status=0;pid_t done=waitpid(rearm_child,&status,WNOHANG);
 if(!done||(done<0&&errno==EINTR))return 0;
 int rc=done==rearm_child&&WIFEXITED(status)?WEXITSTATUS(status):1;
 int was_new=rearm_is_new;rearm_child=0;rearm_is_new=0;
 /* Do this even after an error: the old cached state must never remain on
  * screen after the worker has terminated. The event loop then redraws the
  * entire menu; no camera focus handoff or automatic next step is performed. */
 load_raw_status();load_progress_status();
 if(!rc&&raw_status.valid&&raw_status.state==NXL_RAW_NEED_STILL_INIT)
  strcpy(notice,was_new?"NEW SESSION READY - BOOT INIT CAN BE REUSED":"RESET CHECK OK - BOOT INIT CAN BE REUSED");
 else if(rc==75)strcpy(notice,"SESSION BUSY - WAIT FOR THE CURRENT ACTION");
 else if(rc&&progress_status.valid&&!strcmp(progress_status.phase,"RAW_RESOURCE_CHECK")&&!strcmp(progress_status.state,"HELD"))
  snprintf(notice,sizeof notice,"CHECK HELD: %.104s",progress_status.detail);
 else strcpy(notice,was_new?"NEW SESSION UNCONFIRMED - SEE SESSION STATE / LOG":"RESET CHECK FINISHED - SEE SESSION STATE / LOG");
 if(left_panel)srw_row=3;
 nxl_help_reset(&help,ticks());
 launch_event(was_new?"INLINE_NEW_SESSION_REFRESHED":"INLINE_RESET_REFRESHED",rc&&progress_status.valid?progress_status.detail:raw_status.reason,rc,0);
 return 1;
}
