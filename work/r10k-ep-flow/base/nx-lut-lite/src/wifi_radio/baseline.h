static int baseline(void){
 if(physical_usb()!=0||!supported()||!fingerprints())return failed("BASELINE_NOT_SUPPORTED");
 struct hot_state s;if(!hot_snapshot(&s,0)||s.state||s.owned||s.phase||lstat(HOT_OWNER,&(struct stat){0})==0||lstat(WIFI_PID,&(struct stat){0})==0)return failed("STOP_EXISTING_WIFI_FIRST");trial.camera=s.pid;trial.camera_tick=s.tick;
 struct stat st;if(!lstat("/opt/etc/softap_channel.conf",&st)||!lstat("/sdcard/wifi-test.tg",&st)||!lstat("/mnt/mmc/wifi_oper_ch.txt",&st))return failed("FACTORY_TEST_OVERRIDE_PRESENT");
 trial.old_power=card_state();if(trial.old_power<0)return 0;
 char region[16];if(!read_word_text("/opt/etc/wl-regdom.conf",region,sizeof region)||strlen(region)!=2)return failed("REGION_UNAVAILABLE");strcpy(trial.region,region);for(unsigned i=0;i<2;i++)if(!((trial.region[i]>='A'&&trial.region[i]<='Z')||trial.region[i]=='0'))return failed("REGION_INVALID");
 char country[96];snprintf(country,sizeof country,"RADIO_COUNTRY factory=%s trial=CN",trial.region);note(country);
 char mac[32];if(!read_word_text("/opt/etc/.mac.info",mac,sizeof mac)||strlen(mac)!=17)return failed("FACTORY_MAC_UNAVAILABLE");for(unsigned i=0;i<17;i++)if(i%3==2?mac[i]!=':':!strchr("0123456789abcdefABCDEF",mac[i]))return failed("FACTORY_MAC_INVALID");strcpy(trial.mac,mac);
 if(!radio_baseline(trial.old_power))return 0;trial.initial_modules=trial.old_modules;trial.restore_modules=trial.old_power==0?0:trial.old_modules;
 struct band_word w;if(!band_snapshot(&w)||w.value)return failed("DEFAULT_BAND_NOT_TWO_GHZ");trial.old_band=(int)w.value;trial.us_root=w.root;trial.us_address=w.address;trial.us_root_variable=w.root_variable;
 if(!collect_actors())return failed("NETWORK_ACTOR_BASELINE_UNAVAILABLE");for(unsigned i=0;i<4;i++){char b[160];snprintf(b,sizeof b,"ACTOR_BASELINE slot=%u pid=%ld tick=%llu",i,(long)trial.actors[i].pid,trial.actors[i].tick);note(b);}return state_save();
}

