/* SPDX-License-Identifier: AGPL-3.0-only. Proven P4 radio preparation/rollback,
 * integrated into the Wi-Fi key lifecycle. No application core replacement. */
#define main original_wifi_server_main
#include "wifi-server.c"
#undef main
#include "wifi_radio/trial-contract.h"
#include "wifi_radio/probe.h"
#include "wifi_radio/cfg80211_read.h"
#include "wifi_radio/user_band.h"
static int fingerprints(void){
 if(!hot_fingerprint(HOT_APP,HOT_APP_BYTES,HOT_APP_FNV))return failed("ORIGINAL_CAMERA_APP_CHANGED");
 for(unsigned i=0;i<sizeof trial_files/sizeof *trial_files;i++){
  if(hot_fingerprint(trial_files[i].path,trial_files[i].size,trial_files[i].hash))continue;
  fprintf(stderr,"RADIO_COMPONENT_MISMATCH path=%s expected_bytes=%u\n",trial_files[i].path,trial_files[i].size);
  return failed(!strcmp(trial_files[i].path,RVF_EXE)?"WIFI_SERVER_IDENTITY_MISMATCH":"ORIGINAL_FILE_CHANGED");
 }
 return 1;
}
#include "wifi_radio/radio_power.h"
#include "wifi_radio/native_status.h"
#define RADIO_INSTALLED INSTALLED "/bin/nxl-wifi-radio"
static int live_identity(pid_t pid,unsigned long long tick);
static int owned_state_read(void);
static int hot_same_epoch(struct hot_state *s);
#include "wifi_radio/ap_launch.h"
#include "wifi_radio/ap_width.h"
#include "wifi_rvf_ap.h"
#include "wifi_radio/rvf_ap_profile.h"
#include "wifi_radio/flow.h"
#include "wifi_radio/diagnostic.h"
#include "wifi_rvf_rf.inc"
/* The baseline is the P4 function, included with no memory/firmware changes. */
#include "wifi_radio/baseline.h"
#include "aba_original_contract.h"
#include "aba_engine.h"
#include "wifi_radio/radio_active.h"
#define RADIO_VERSION "0.4.0-RC6.14.10"
#define RADIO_MODULE INSTALLED "/wireless/cfg80211-cn-low5g.ko"
static int reply_fd=-1;
static void radio_signal(int s){(void)s;trial_stop=1;}
static int live_identity(pid_t pid,unsigned long long tick){
 if(pid<=1||!tick||proc_ticks(pid)!=tick)return 0;char p[80],exe[PATH_MAX];snprintf(p,sizeof p,"/proc/%ld/exe",(long)pid);ssize_t n=readlink(p,exe,sizeof exe-1);if(n<=0)return 0;exe[n]=0;return !strcmp(exe,RADIO_INSTALLED);
}
static int done_clean(void){return trial.phase==P_DONE&&!trial.module_dirty&&!trial.band_dirty&&!trial.native_unconfirmed&&!trial.aba.dirty&&!trial.ap_pending;}
static int process_present(pid_t pid,unsigned long long tick){return pid>1&&tick&&proc_ticks(pid)==tick&&actor_state(pid)!='Z';}
static int never_mutated(void){if(trial.phase>P_SNAPSHOT||trial.module_dirty||trial.band_dirty||trial.native_unconfirmed||trial.child||trial.ap_pending)return 0;for(unsigned i=0;i<4;i++)if(trial.actors[i].frozen)return 0;return 1;}
static int rvf_radio_loop(void){
 unsigned long long heartbeat=monotime()+10000;
 while(!trial_stop){
  struct rvf_owner owner;
  if(!radio_rvf_owner(&owner)||owner.pid!=trial.rvf_pid||owner.tick!=trial.rvf_tick||
     !wire_process(owner.pid,owner.tick,RVF_EXE))return failed("RVF_RADIO_OWNER_LOST");
  unsigned long long now=monotime();
  if(!trial.primary_reason[0] && owner.phase==RVF_ENTERING && trial.phase==P_READY &&
     owner.ap_up>=0 && ap_rvf_has_frame(&owner) && frequency_mhz("uap0")==NXL_AP_MHZ){
   char ip[32];unsigned index;
   if(rvf_ap_link(&owner,ip,&index)){
    if(ap_confirm_active("RVF"))ap_publish_rvf_profile(&owner,ip,index);
    else {
     snprintf(trial.primary_reason,sizeof trial.primary_reason,"%s",error_reason);
     (void)ap_release_launch();
     if(!state_save())return failed("ACTIVE_JOURNAL_WRITE_FAILED");
    }
   }
  }
  if(now>=heartbeat){
   if(!state_save())return failed("ACTIVE_JOURNAL_WRITE_FAILED");
   heartbeat=now+10000;
  }
  poll(NULL,0,250);
 }
 return 1;
}
static int owned_state_read(void){
 if(!state_read())return 0;if(trial.owner<=1||!trial.owner_tick||!trial.report[0]||strncmp(trial.report,"/mnt/mmc/NXDEMO51/WIFI_RADIO_",sizeof("/mnt/mmc/NXDEMO51/WIFI_RADIO_")-1)||strstr(trial.report,".."))return 0;return 1;
}
static int ram_module(void){
 const char*to=TRIAL_TMP "/cfg80211-cn-low5g.ko";
 if(!hot_fingerprint(RADIO_MODULE,RADIO_MODULE_BYTES,RADIO_MODULE_FNV))return failed("INSTALLED_CN_TABLE_MISMATCH");
 struct stat st;if(!lstat(to,&st))return hot_fingerprint(to,RADIO_MODULE_BYTES,RADIO_MODULE_FNV)?1:failed("RAM_CN_TABLE_MISMATCH");
 if(errno!=ENOENT)return failed("RAM_CN_TABLE_UNREADABLE");
 int src=open(RADIO_MODULE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW),dst=open(to,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);int ok=src>=0&&dst>=0;unsigned char b[4096];size_t total=0;
 while(ok&&total<RADIO_MODULE_BYTES){size_t need=RADIO_MODULE_BYTES-total;if(need>sizeof b)need=sizeof b;ssize_t n=read(src,b,need);if(n<=0){ok=0;break;}size_t at=0;while(at<(size_t)n){ssize_t k=write(dst,b+at,(size_t)n-at);if(k<=0){ok=0;break;}at+=(size_t)k;}total+=(size_t)n;}
 if(dst>=0){if(fsync(dst))ok=0;if(close(dst))ok=0;}if(src>=0&&close(src))ok=0;
 return ok&&hot_fingerprint(to,RADIO_MODULE_BYTES,RADIO_MODULE_FNV)?1:failed("RAM_CN_TABLE_COPY_FAILED");
}
static int report_begin(void){
 snprintf(trial.report,sizeof trial.report,"/mnt/mmc/NXDEMO51/WIFI_RADIO_%llu_%ld",monotime(),(long)getpid());
 if(physical_usb()!=0||mkdir(trial.report,0700))return failed("RADIO_REPORT_DIRECTORY_FAILED");char p[640];snprintf(p,sizeof p,"%s/start.txt",trial.report);int f=open(p,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;
 int ok=dprintf(f,"format=NXL_RADIO_RC610\nversion=" RADIO_VERSION "\ncountry=CN\nexpected_frequency_mhz=" NXL_AP_MHZ_TEXT "\nprimary_channel=" NXL_AP_PRIMARY "\nsecondary_channel=" NXL_AP_SECONDARY "\ncenter_channel=" NXL_AP_CENTER "\nwidth_mhz=" NXL_AP_WIDTH "\nphy_mode=HT40\nmodule_sha256=" RADIO_MODULE_SHA "\nnormal_session_timeout=none\n")>0&&!fsync(f);if(close(f))ok=0;return ok;
}
static const char*save_dir;
static void report_file(const char*source,const char*leaf){
 if(physical_usb()!=0||!trial.report[0])return;char p[640],tmp[720];struct stat st;snprintf(p,sizeof p,"%s/%s",save_dir?save_dir:trial.report,leaf);if(!lstat(p,&st)||errno!=ENOENT)return;snprintf(tmp,sizeof tmp,"%s.partial-%ld-%llu",p,(long)getpid(),monotime());int src=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(src<0)return;int dst=open(tmp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(dst<0){close(src);return;}
 char b[4096];size_t total=0;int ok=1;while(total<262144&&physical_usb()==0){ssize_t n=read(src,b,sizeof b);if(n==0)break;if(n<0){ok=0;break;}size_t at=0;while(at<(size_t)n){ssize_t k=write(dst,b+at,(size_t)n-at);if(k<=0){ok=0;break;}at+=(size_t)k;}if(!ok)break;total+=(size_t)n;}
 if(physical_usb()!=0||fsync(dst))ok=0;if(close(dst))ok=0;close(src);if(ok&&physical_usb()==0&&lstat(p,&st)&&errno==ENOENT)(void)rename(tmp,p);
}
static void aba_result_file(void){
 if(!trial.aba.enabled)return;char path[200];snprintf(path,sizeof path,TRIAL_TMP "/aba-result.txt");int f=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return;struct aba_data*a=&trial.aba;
 dprintf(f,"format=NXL_MGMT_ABA_RESULT1\nversion=0.4.0-RC6.12\nstate=%u\nreason=%u\noriginal_saved=%u\noriginal_mask=0x%x\nB_mask=0x%x\nmask_may_be_changed=%u\nchanged_verified=%u\noriginal_readback_restored=%u\nverified_driver_teardown_cleanup=%u\nfrequency_expected_mhz=5180\nrate_mode_required=Auto\n",a->state,a->reason,a->saved,a->original,a->bmask,a->dirty,a->changed,a->restored,a->reset_cleanup);
 for(unsigned i=0;i<3;i++){struct aba_stage*p=a->stages+i;dprintf(f,"stage_%u_start_ms=%llu\nstage_%u_end_ms=%llu\nstage_%u_body_bytes=%llu\nstage_%u_net_bytes=%llu\nstage_%u_frames=%llu\nstage_%u_progress_samples=%u\nstage_%u_valid=%u\n",i,p->start,i,p->end,i,p->body1>=p->body0?p->body1-p->body0:0,i,p->net1>=p->net0?p->net1-p->net0:0,i,p->frames1>=p->frames0?p->frames1-p->frames0:0,i,p->samples,i,p->valid);}
 dprintf(f,"end=NXL_MGMT_ABA_RESULT_END1\n");fsync(f);close(f);
}

static void report_end(int ok){
 aba_result_file();
 if(physical_usb()!=0||!trial.report[0])return;char p[640],dir[600],tmp[720],old[640];snprintf(dir,sizeof dir,"%s",trial.report);snprintf(p,sizeof p,"%s/result.txt",dir);
 int prior=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);int existing=prior>=0,was_clean=0;if(prior>=0){ssize_t n=read(prior,old,sizeof old-1);close(prior);if(n>0){old[n]=0;was_clean=strstr(old,"\nphase=9\nrestored=1\n")&&strstr(old,"end=NXL_RADIO_END1\n");}}
 if(existing&&!was_clean){snprintf(dir,sizeof dir,"%s/recovery-%ld-%llu",trial.report,(long)getpid(),monotime());if(mkdir(dir,0700))return;snprintf(p,sizeof p,"%s/result.txt",dir);existing=0;}
 save_dir=dir;report_file(TRIAL_TMP "/controller.log","radio.log");report_file(WIFI_TMP "/wifi-service.log","transport.log");report_file(RVF_STATE,"rvf-state.bin");report_file(RVF_OWNER,"rvf-owner.bin");if(trial.aba.enabled){const char*names[]={"aba-A1-start.txt","aba-A1-end.txt","aba-B-end.txt","aba-A2-end.txt","aba-result.txt"};for(unsigned i=0;i<5;i++){char src[200];snprintf(src,sizeof src,TRIAL_TMP "/%s",names[i]);report_file(src,names[i]);}}save_dir=NULL;
 if(existing)return;snprintf(tmp,sizeof tmp,"%s.partial-%ld-%llu",p,(long)getpid(),monotime());int f=open(tmp,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return;
 int written=dprintf(f,"format=NXL_RADIO_RC610\nversion=" RADIO_VERSION "\nphase=%d\nrestored=%d\nreason=%s\nprimary_reason=%s\nmodule_dirty=%d\nband_dirty=%d\nnative_unconfirmed=%d\nnormal_session_timeout=none\nend=NXL_RADIO_END1\n",trial.phase,ok,error_reason[0]?error_reason:"NONE",trial.primary_reason,trial.module_dirty,trial.band_dirty,trial.native_unconfirmed)>0;
 if(physical_usb()!=0||fsync(f))written=0;if(close(f))written=0;struct stat st;if(written&&physical_usb()==0&&lstat(p,&st)&&errno==ENOENT)(void)rename(tmp,p);
}
static void send_ready(int ok){if(reply_fd>=0){char c=ok?'1':'0';if(write(reply_fd,&c,1)!=1)trial_stop=1;close(reply_fd);reply_fd=-1;}}
static int early_failure(void){
 if(error_reason[0])snprintf(trial.primary_reason,sizeof trial.primary_reason,"%s",error_reason);(void)phase_save(P_DONE);report_end(1);release_capture_lock();send_ready(0);return 1;
}
static int finish_radio(void){
 if(error_reason[0]&&!trial.primary_reason[0])snprintf(trial.primary_reason,sizeof trial.primary_reason,"%s",error_reason);
 if(!state_save())note("FINAL_PRIMARY_REASON_NOT_SAVED");trial_stop=0;int mask_ok=aba_finish();int ok=ap_release_launch()&&(never_mutated()?phase_save(P_DONE):restore_trial());
 aba_teardown(ok);if(trial.aba.dirty)ok=0;if(!mask_ok)note("MGMT_ABA_DIRECT_RESTORE_NOT_CONFIRMED see_parameter_cleanup_result=1");if(!ok){thaw_actors();diagnostic_snapshot("RADIO_RESTORE_FAILURE");phase_save(P_HOLD);}report_end(ok);return ok;
}
static void guard_loop(pid_t owner,unsigned long long tick){
 if(reply_fd>=0){close(reply_fd);reply_fd=-1;}if(mutation>=0)close(mutation);release_capture_lock();mutation=open(TRIAL_LOCK,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(mutation<0)_exit(2);
 for(;;){if(!owned_state_read())_exit(3);if(trial.owner!=owner||trial.owner_tick!=tick)_exit(4);if(done_clean()||trial.phase==P_HOLD)_exit(0);
  unsigned long long now=monotime(),budget=trial.phase<P_READY?70000:trial.phase>=P_RESTORE?70000:35000;
  if(!live_identity(owner,tick)){if(process_present(owner,tick)){note("OWNER_IDENTITY_CHANGED_NO_FORCED_RECOVERY");_exit(8);}break;}if(now>trial.heartbeat+budget)break;poll(NULL,0,500);
 }
 if(live_identity(owner,tick)){kill(owner,SIGTERM);poll(NULL,0,500);if(live_identity(owner,tick)){kill(owner,SIGKILL);poll(NULL,0,1000);}if(live_identity(owner,tick)&&actor_state(owner)!='Z'){note("OWNER_STUCK_NO_FORCED_RADIO_RECOVERY");_exit(5);}}
 unsigned long long end=monotime()+3000;while(flock(mutation,LOCK_EX|LOCK_NB)){if(monotime()>=end)_exit(6);poll(NULL,0,50);}if(!owned_state_read()||trial.owner!=owner||trial.owner_tick!=tick)_exit(7);
 note("RADIO_WATCHDOG_RECOVERY");trial_stop=0;if(trial.child&&proc_ticks(trial.child)==trial.child_tick){kill(trial.child,SIGTERM);poll(NULL,0,200);if(proc_ticks(trial.child)==trial.child_tick){kill(trial.child,SIGKILL);poll(NULL,0,300);}if(proc_ticks(trial.child)!=trial.child_tick){trial.child=0;trial.child_tick=0;}}
 int ok=finish_radio();_exit(ok?0:1);
}
static int daemon_run(void){
 if(setsid()<0&&getsid(0)!=getpid())return 1;
 memset(&trial,0,sizeof trial);trial.old_power=-1;trial.owner=getpid();trial.owner_tick=proc_ticks(getpid());strcpy(trial.package,INSTALLED "/bin");snprintf(trial.report,sizeof trial.report,"/mnt/mmc/NXDEMO51/WIFI_RADIO_%llu_%ld",monotime(),(long)getpid());
 if(!trial.owner_tick||!state_save()){send_ready(0);return 1;}
 int log=open(TRIAL_TMP "/controller.log",O_CREAT|O_EXCL|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOFOLLOW,0600);if(log<0){failed("RADIO_LOG_OPEN_FAILED");return early_failure();}
 if(dup2(log,1)<0||dup2(log,2)<0){close(log);phase_save(P_DONE);send_ready(0);return 1;}close(log);setvbuf(stdout,NULL,_IONBF,0);
 if(!report_begin()||!state_save()){if(!error_reason[0])failed("RADIO_REPORT_OR_JOURNAL_FAILED");return early_failure();}
 capture_lock=run_guard();if(capture_lock<0){failed("SRW_BUSY_NO_RADIO_PREPARATION");return early_failure();}
 if(!baseline()||!ram_module())return early_failure();
 if(trial.rvf_pid){
  struct rvf_owner owner;
  if(!radio_rvf_owner(&owner)||owner.phase!=RVF_ENTERING||owner.ap_up>=0||
     owner.pid!=trial.rvf_pid||owner.tick!=trial.rvf_tick||!wire_process(owner.pid,owner.tick,RVF_EXE)){
   failed("RVF_RADIO_OWNER_CHANGED");return early_failure();
  }
 }
 if(trial_stop||physical_usb()!=0){failed("CANCELLED_BEFORE_RADIO_PREPARATION");return early_failure();}
 pid_t g=fork();if(g<0){failed("WATCHDOG_UNAVAILABLE");return early_failure();}if(!g)guard_loop(trial.owner,trial.owner_tick);trial.guard=g;trial.guard_tick=proc_ticks(g);
 if(!trial.guard_tick||!state_save()){failed("WATCHDOG_NOT_JOURNALED");return early_failure();}
 int prepared=prepare_trial();if(!prepared)diagnostic_snapshot("RADIO_PREPARE_FAILURE");release_capture_lock();
 if(prepared){flock(mutation,LOCK_UN);send_ready(1);if(!trial_stop){
  if(trial.rvf_pid)(void)rvf_radio_loop();
  else (void)radio_active_loop();
 }unsigned long long until=monotime()+5000;while(flock(mutation,LOCK_EX|LOCK_NB)){if(monotime()>until)return 1;poll(NULL,0,50);}}
 else send_ready(0);
 int ok=finish_radio();close(mutation);waitpid(g,NULL,WNOHANG);return ok&&prepared?0:1;
}
static int private_archive(const char*from,const char*to){
 struct stat st;if(lstat(from,&st))return errno==ENOENT;if(!S_ISREG(st.st_mode))return 0;if(!lstat(to,&st)||errno!=ENOENT)return 0;return !rename(from,to);
}
static int prior_archive(void){
 struct stat st;if(lstat(TRIAL_STATE,&st)){if(errno!=ENOENT)return 0;return lstat(TRIAL_TMP "/controller.log",&st)&&errno==ENOENT;}
 if(!owned_state_read()||!done_clean()||process_present(trial.owner,trial.owner_tick))return 0;
 unsigned long long until=monotime()+2000;while(process_present(trial.guard,trial.guard_tick)){if(monotime()>=until)return 0;poll(NULL,0,50);}
 report_end(1);char p[200];snprintf(p,sizeof p,TRIAL_TMP "/controller-%ld-%llu.log",(long)trial.owner,trial.owner_tick);if(!private_archive(TRIAL_TMP "/controller.log",p))return 0;
 if(!lstat(TRIAL_TMP "/cfg80211-cn-low5g.ko",&st)&&!hot_fingerprint(TRIAL_TMP "/cfg80211-cn-low5g.ko",RADIO_MODULE_BYTES,RADIO_MODULE_FNV)){snprintf(p,sizeof p,TRIAL_TMP "/bad-module-%ld-%llu.bin",(long)trial.owner,trial.owner_tick);if(!private_archive(TRIAL_TMP "/cfg80211-cn-low5g.ko",p))return 0;}
 const char*aba_files[]={"aba-A1-start.txt","aba-A1-end.txt","aba-B-end.txt","aba-A2-end.txt","aba-result.txt","aba-cli.txt"};for(unsigned i=0;i<6;i++){char from[200],to[240];snprintf(from,sizeof from,TRIAL_TMP "/%s",aba_files[i]);snprintf(to,sizeof to,TRIAL_TMP "/old-%ld-%llu-%s",(long)trial.owner,trial.owner_tick,aba_files[i]);if(!private_archive(from,to))return 0;}
 /* The clean journal is the commit record and moves last. An interrupted
  * archive can be resumed without losing the proof that hardware was idle. */
 snprintf(p,sizeof p,TRIAL_TMP "/state-%ld-%llu.bin",(long)trial.owner,trial.owner_tick);return private_archive(TRIAL_STATE,p);
}
static int prepare_command(void){
 if(physical_usb()!=0||!supported())return 1;if(mkdir(TRIAL_TMP,0700)&&errno!=EEXIST)return 1;struct stat st;if(lstat(TRIAL_TMP,&st)||!S_ISDIR(st.st_mode))return 1;
 mutation=open(TRIAL_LOCK,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(mutation<0||flock(mutation,LOCK_EX|LOCK_NB))return 1;
 if(owned_state_read()&&live_identity(trial.owner,trial.owner_tick)){
  int ok=!trial.rvf_pid&&
      (trial.phase==P_READY||trial.phase==P_ACTIVE)&&module_mask()==7&&interface_present("uap0")==1;
  close(mutation);return !ok;
 }
 if(owned_state_read()&&!process_present(trial.owner,trial.owner_tick)&&!process_present(trial.guard,trial.guard_tick)&&never_mutated()){if(!phase_save(P_DONE)){close(mutation);return 1;}report_end(1);}
 if(!prior_archive()){close(mutation);return 1;}
 int p[2];if(pipe2(p,O_CLOEXEC)){close(mutation);return 1;}pid_t child=fork();if(child<0){close(p[0]);close(p[1]);close(mutation);return 1;}
 if(!child){close(p[0]);reply_fd=p[1];int null=open("/dev/null",O_RDONLY|O_CLOEXEC);if(null>=0){dup2(null,0);close(null);}int r=daemon_run();_exit(r);}
 close(p[1]);close(mutation);mutation=-1;unsigned long long until=monotime()+55000;int ok=0;
 while(!trial_stop&&monotime()<until){struct pollfd f={p[0],POLLIN|POLLHUP,0};int n=poll(&f,1,100);if(n>0&&(f.revents&(POLLIN|POLLHUP))){char c;if(read(p[0],&c,1)==1&&c=='1')ok=1;break;}if(n<0&&errno!=EINTR)break;}
 close(p[0]);waitpid(child,NULL,WNOHANG);return !ok;
}
static int stop_command(int rvf_release){
 if(!owned_state_read()){struct stat st;return lstat(TRIAL_STATE,&st)&&errno==ENOENT?0:1;}
 if(rvf_release&&(!trial.rvf_pid||done_clean()))return 0;
 if(rvf_release&&!radio_rvf_restore_gate())return 1;
 if(!rvf_release&&trial.rvf_pid&&!done_clean())return 1;
 pid_t owner=trial.owner;unsigned long long tick=trial.owner_tick;
 if(live_identity(owner,tick)){if(kill(owner,SIGTERM))return 1;unsigned long long until=monotime()+30000;
  while(monotime()<until){if(!owned_state_read()||trial.owner!=owner||trial.owner_tick!=tick)return 1;if(done_clean()&&(!live_identity(owner,tick)||actor_state(owner)=='Z')){report_end(1);return 0;}if(trial.phase==P_HOLD)return 1;poll(NULL,0,100);}return 1;
 }
 if(done_clean()){report_end(1);return 0;}
 if(process_present(owner,tick))return 1;
 /* A live watchdog owns recovery; do not race it with another restorer. */
 if(process_present(trial.guard,trial.guard_tick))return 1;
 mutation=open(TRIAL_LOCK,O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600);if(mutation<0||flock(mutation,LOCK_EX|LOCK_NB))return 1;
 if(!owned_state_read()||live_identity(trial.owner,trial.owner_tick)){close(mutation);return 1;}
 int f=open(TRIAL_TMP "/controller.log",O_WRONLY|O_APPEND|O_CLOEXEC|O_NOFOLLOW);if(f>=0){dup2(f,1);dup2(f,2);close(f);}int ok=finish_radio();close(mutation);return !ok;
}
static int confirm_profile(void){
 unsigned long long end=monotime()+7500;
 do{
  if(physical_usb()!=0||!owned_state_read()||!live_identity(trial.owner,trial.owner_tick)||trial.primary_reason[0])return 1;
  if(trial.phase==P_ACTIVE&&!trial.ap_pending)return frequency_mhz("uap0")!=NXL_AP_MHZ;
  if(trial.phase!=P_READY)return 1;
  poll(NULL,0,120);
 }while(monotime()<end);
 return 1;
}
static int stock_radio_ready(void){
 if(physical_usb()!=0)return 0;
 if(!owned_state_read()){struct stat st;return lstat(TRIAL_STATE,&st)&&errno==ENOENT;}
 return done_clean()&&!process_present(trial.owner,trial.owner_tick);
}

int main(int argc,char**argv){
 if(argc>0&&(!strcmp(argv[0],NXL_AP_DAEMON)||!strcmp(argv[0],"hostapd")))return ap_relay(argc,argv);
 if(argc==2&&!strcmp(argv[1],"--read-width"))return !ap_read_width();
 if(argc==2&&!strcmp(argv[1],"--confirm-profile"))return confirm_profile();
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_RADIO_VERSION=" RADIO_VERSION "; CN_CH48_HT40_MINUS; OEM_ROLLBACK");return 0;}
 signal(SIGPIPE,SIG_IGN);signal(SIGHUP,SIG_IGN);signal(SIGTERM,radio_signal);signal(SIGINT,radio_signal);
 if(argc!=2)return 2;
 if(!strcmp(argv[1],"--stock-album-ready"))return !stock_radio_ready();
 if(!strcmp(argv[1],"--stock-ready")||!strcmp(argv[1],"--read-rvf-power")){
  if(physical_usb()!=0||!rvf_owner_read(&native)||native.pid!=getppid()||
     native.phase!=RVF_ENTERING||!wire_process(native.pid,native.tick,RVF_EXE))return 1;
  if(!strcmp(argv[1],"--read-rvf-power")){
   signal(SIGTERM,SIG_DFL);
   rvf_read_power();return 0;
  }
  return !stock_radio_ready();
 }
 int prep=!strcmp(argv[1],"--prepare"),stop=!strcmp(argv[1],"--stop");
 int rvf_stop=!strcmp(argv[1],"--stop-rvf");
 if(rvf_stop){
  /* Older releases can leave a journal that still needs restoration. */
  struct rvf_owner owner;
  if(!rvf_owner_read(&owner)||owner.pid!=getppid()||
     !wire_process(owner.pid,owner.tick,RVF_EXE)||owner.phase!=RVF_RADIO_RESTORE)return 1;
 }
 if(prep||stop||rvf_stop){
  int r=prep?prepare_command():stop_command(rvf_stop);
  if(r){if(owned_state_read())fprintf(stderr,"NXL_RADIO_ERROR phase=%d reason=%s\n",trial.phase,trial.primary_reason[0]?trial.primary_reason:"RADIO_STATE_NOT_READY");else fputs("NXL_RADIO_ERROR reason=RADIO_JOURNAL_OR_PRECONDITION_UNAVAILABLE\n",stderr);}
  return r;
 }
 if(!strcmp(argv[1],"--status")){if(!owned_state_read()){puts("NXL_RADIO1 state=absent");return 0;}printf("NXL_RADIO1 phase=%d clean=%d owner_live=%d frequency_expected_mhz=" NXL_AP_MHZ_TEXT "\n",trial.phase,done_clean(),live_identity(trial.owner,trial.owner_tick));return 0;}return 2;
}
