/* SPDX-License-Identifier: AGPL-3.0-only */
static int hot_same_epoch(struct hot_state*s){return hot_snapshot(s,0)&&s->pid==trial.camera&&s->tick==trial.camera_tick;}
static void hot_note(const char*label,const struct hot_state*s){char line[220];snprintf(line,sizeof line,"%s state=%d owned=%d phase=%d ready=%d callback=%08x",label,s->state,s->owned,s->phase,s->ready,s->callback);note(line);}
static int no_radio_hardware(void){return module_mask()==0&&interface_present("mlan0")==0&&interface_present("uap0")==0&&interface_present("p2p0")==0&&loaders_idle();}
static int prepare_trial(void){
 if(!phase_save(P_SNAPSHOT)||!freeze_actors())return 0;
 struct hot_state s;if(!hot_same_epoch(&s)||s.state||s.owned||s.phase)return failed("NATIVE_BASELINE_CHANGED_BEFORE_PREPARE");
 if(module_mask()!=trial.initial_modules)return failed("RADIO_BASELINE_CHANGED_BEFORE_LOAD");
 trial.module_dirty=1;if(!phase_save(P_POWER)||!radio_power_on())return 0;
 if(!phase_save(P_UNLOAD)||!unload_driver())return 0;
 if(!phase_save(P_LOAD)||!load_driver(1))return failed("FIVE_GHZ_MODULE_LOAD_FAILED");
 int legacy=range_table(1);if(trial.child)return failed("LEGACY_QUERY_CHILD_PENDING");if(!legacy)note("LEGACY_UAP_BSS_LIST_IS_NOT_PHY_CAPABILITY");error_reason[0]=0;
 if(!cfg80211_ready())return 0;
 if(!hot_same_epoch(&s)||s.state||s.owned)return failed("NATIVE_BASELINE_CHANGED_BEFORE_SET");
 trial.band_dirty=1;if(!phase_save(P_SET)||!band_exchange(trial.old_band,1))return 0;
 if(physical_usb()!=0||trial_stop)return failed("AP_LAUNCH_CANCELLED");
 if(!ap_hold_launch())return 0;
 if(!thaw_actors())return failed("NETWORK_RESUME_FAILED");
 if(module_mask()!=7||interface_present("uap0")!=1)return failed("PREPARED_DRIVER_CHANGED_AFTER_RESUME");
 if(!hot_same_epoch(&s)||s.state||s.owned)return failed("NATIVE_BASELINE_CHANGED_BEFORE_READY");hot_note("PREPARED_WITHOUT_AP",&s);
 return phase_save(P_READY);
}
static int restore_trial(void){
 /* The RVF supervisor owns its native teardown. Radio rollback must
  * wait for its journal to confirm release before touching driver state. */
 if(!radio_rvf_restore_gate())return failed("RVF_RESTORE_UNCONFIRMED");
 if(!take_capture_lock()||!phase_save(P_RESTORE))return 0;
 if(trial.child&&proc_ticks(trial.child)==trial.child_tick)return failed("CHILD_PENDING_NEEDS_BATTERY");
 struct hot_state s;if(!hot_same_epoch(&s))return failed("CAMERA_CHANGED_NEEDS_BATTERY");hot_note("RESTORE_NATIVE_BEFORE",&s);int native_ok=1,foreign=0;
 if(s.state||s.owned||s.phase){
  if(trial.rvf_pid)return failed("RVF_NATIVE_NETWORK_NOT_IDLE");
  if(!s.owned){native_ok=0;foreign=1;note("FOREIGN_NATIVE_STATE_NOT_STOPPED");}
  else{const char*a[]={INSTALLED "/bin/nxl-wifi-server","--stop",NULL};int stopped=command(a,16000);native_stop_status(native_status_child);
   if(!hot_same_epoch(&s))return failed("CAMERA_CHANGED_AFTER_NATIVE_STOP");native_ok=!s.state&&!s.owned&&!s.phase;if(!native_ok)note("NATIVE_STOP_STILL_UNCONFIRMED");else if(!stopped)note("NATIVE_STOP_COMMAND_FAILED_STATE_CONFIRMED");hot_note("RESTORE_NATIVE_AFTER",&s);
  }
 }
 trial.native_unconfirmed=!native_ok;
 if(foreign)return failed("FOREIGN_NATIVE_STATE_PRESERVED");
 if(!native_ok){
  if(!freeze_actors())return failed("PENDING_NATIVE_NETWORK_NOT_QUIESCED");
  if(!no_radio_hardware())return failed("PENDING_NATIVE_HARDWARE_NOT_IDLE");poll(NULL,0,500);
  if(!hot_same_epoch(&s)||!no_radio_hardware())return failed("PENDING_NATIVE_HARDWARE_CHANGED");
 }
 /* Our preference lease can be returned even if the application UI is stuck.
  * Its PID/root/value guards remain mandatory. Do not clear its owner or state. */
 if(trial.band_dirty){struct band_word w;if(!band_snapshot_restore(&w))return 0;if(w.value!=(uint32_t)trial.old_band&&!band_restore())return 0;trial.band_dirty=0;if(!state_save())return 0;note("BAND_LEASE_RETURNED");}
 if(trial.module_dirty){
  if(!native_ok){
   if(trial.restore_modules!=0)return failed("NATIVE_PENDING_DEFAULT_STA_NOT_RECREATED");
   trial.module_dirty=0;if(!state_save())return 0;note("DRIVER_OVERRIDE_ABSENT_NATIVE_UI_PENDING");
  }else{
   if(!freeze_actors()||!unload_driver())return failed("DEFAULT_DRIVER_NOT_RESTORED");
   if(trial.restore_modules==7){
    if(card_state()!=1||!load_driver(0)||!range_table(0))return failed("DEFAULT_DRIVER_NOT_RESTORED");
    const char*a[]={"/usr/bin/mlanutl","mlan0","htcapinfo","0x05820000","1",NULL};const char*b[]={"/usr/bin/mlanutl","mlan0","httxcfg","0x62","1",NULL};if(!command(a,3000)||!command(b,3000))return failed("DEFAULT_HT_SETTINGS_NOT_RESTORED");
   }else if(trial.restore_modules!=0||!no_radio_hardware())return failed("UNLOADED_BASELINE_NOT_RESTORED");
   trial.module_dirty=0;if(!state_save())return 0;
  }
 }
 if(!thaw_actors())return failed("RESTORE_NETWORK_RESUME_FAILED");
 if(!native_ok)return failed("NATIVE_UI_PENDING_DRIVER_AND_BAND_CHECKED");
 struct stat scratch;if(!lstat("/opt/etc/softap_channel.conf",&scratch))return failed("FACTORY_CHANNEL_SCRATCH_REMAINS");
 if(!ap_remove_config())return failed("AP_PRIVATE_CONFIG_NOT_REMOVED");
 if(!phase_save(P_DONE))return 0;note("FACTORY_BAND_AND_DRIVER_RESTORED");return 1;
}
