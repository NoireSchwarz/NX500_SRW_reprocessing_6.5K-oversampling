/* No normal active-session deadline. Failures still have bounded recovery. */
struct radio_run{unsigned long long begin,first_active,last_seen,pending,next_heartbeat;unsigned long long frequency_reads;unsigned observations;int seen,last_state,last_owned,last_phase;};
static int radio_active_loop(void){
 struct radio_run r;memset(&r,0,sizeof r);r.begin=monotime();r.next_heartbeat=r.begin+10000;r.last_state=r.last_owned=r.last_phase=-99;
 while(!trial_stop){unsigned long long now=monotime();if(physical_usb()!=0){note("RADIO_USB_EXIT");break;}
  if(proc_ticks(trial.guard)!=trial.guard_tick)return failed("WATCHDOG_LOST_DURING_USE");
  struct hot_state s;if(!hot_same_epoch(&s))return failed("CAMERA_EPOCH_CHANGED");
  if(s.state!=r.last_state||s.owned!=r.last_owned||s.phase!=r.last_phase){if(r.observations++<32)hot_note("RADIO_NATIVE_STATE",&s);r.last_state=s.state;r.last_owned=s.owned;r.last_phase=s.phase;}
  if(s.state&&!s.owned)return failed("FOREIGN_NATIVE_AP_DURING_USE");
  if(!s.state&&!s.owned&&(module_mask()!=7||interface_present("uap0")!=1))return r.seen?failed("ACTIVE_DRIVER_DISAPPEARED"):failed("PREPARED_DRIVER_DISAPPEARED");
  if(s.owned&&s.ready){if(module_mask()!=7||interface_present("uap0")!=1)return failed("ACTIVE_DRIVER_DISAPPEARED");int mhz=frequency_mhz("uap0");r.frequency_reads++;if(mhz!=NXL_AP_MHZ){
    if(r.seen||now-r.begin>45000)return failed("ACTIVE_FREQUENCY_MISMATCH");
   }else{
    if(!r.seen){if(!ap_confirm_active("ALBUM"))return 0;r.seen=1;r.first_active=now;}
    r.last_seen=now;r.pending=0;
   }
  }else if(r.seen&&!s.state&&!s.owned){note("RADIO_NATIVE_CLOSED");break;}
  else if(s.owned){if(!r.pending)r.pending=now;if(now-r.pending>25000)return failed("NATIVE_AP_START_DID_NOT_COMPLETE");}
  if(!r.seen&&now-r.begin>45000)return failed("HOTSPOT_NOT_STARTED_AFTER_PREPARE");
  if(now>=r.next_heartbeat){if(!state_save())return failed("ACTIVE_JOURNAL_WRITE_FAILED");r.next_heartbeat=now+10000;}
  poll(NULL,0,1000);
 }
 char line[220];snprintf(line,sizeof line,"RADIO_SESSION_END seen=%d active_ms=%llu last_seen_ms=%llu frequency_reads=%llu requested_stop=%d",r.seen,r.seen?monotime()-r.first_active:0,r.last_seen,r.frequency_reads,!!trial_stop);note(line);return 1;
}
