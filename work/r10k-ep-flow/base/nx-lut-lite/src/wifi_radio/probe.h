/* SPDX-License-Identifier: AGPL-3.0-only
 * NX500 1.12 volatile 5GHz preparation and restoration for normal Wi-Fi sessions.
 * The only user setting write is the OEM dual-band common item, then restore.
 * Only the stock 0-to-1 SDIO enable is added; no reset, power-off or calibration edit.
 */
#define TRIAL_TMP "/tmp/nx-lut-lite/wifi-radio"
#define TRIAL_STATE TRIAL_TMP "/state"
#define TRIAL_LOCK TRIAL_TMP "/mutation.lock"
#define INSTALLED "/opt/usr/nx-lut-lite"
/* NX500 mlan maps masked GET_HW_SPEC bit 13 to the 5 GHz 11ac band flag.
 * This mask is used only on the mod AP load. Factory restore has no override. */
#define PROBE_MASK "dev_cap_mask=0xffffcfff"
enum phase{P_NEW,P_SNAPSHOT,P_POWER,P_UNLOAD,P_LOAD,P_SET,P_READY,P_ACTIVE,P_RESTORE,P_DONE,P_HOLD};
struct actor{pid_t pid;unsigned long long tick;char path[96];int frozen;};
#include <stddef.h>
#include "../aba_contract.h"
struct legacy_trial{uint32_t magic,checksum,us_root,us_address,us_root_variable;pid_t owner,guard,camera,child;unsigned long long owner_tick,guard_tick,camera_tick,child_tick,heartbeat;int phase,module_dirty,band_dirty,old_band,old_up,old_modules,initial_modules,old_power,restore_modules,power_on_issued,power_confirmed,native_unconfirmed;char primary_reason[96];struct actor actors[4];char region[3],mac[18],report[512],package[256];};
struct trial{uint32_t magic,checksum,us_root,us_address,us_root_variable;pid_t owner,guard,camera,child;unsigned long long owner_tick,guard_tick,camera_tick,child_tick,heartbeat;int phase,module_dirty,band_dirty,old_band,old_up,old_modules,initial_modules,old_power,restore_modules,power_on_issued,power_confirmed,native_unconfirmed;char primary_reason[96];struct actor actors[4];char region[3],mac[18],report[512],package[256];struct aba_data aba;unsigned long long rvf_tick;pid_t rvf_pid;uint64_t ap_device,ap_inode,relay_device,relay_inode;int ap_fd,ap_pending;};
_Static_assert(offsetof(struct trial,aba)==sizeof(struct legacy_trial),"legacy radio prefix");
#define RADIO_PREVIOUS_BYTES offsetof(struct trial,rvf_tick)
#define RADIO_RC614_BYTES offsetof(struct trial,ap_device)
static struct trial trial;
static int mutation=-1,capture_lock=-1;
static volatile sig_atomic_t trial_stop;
static pid_t native_status_child;
static char error_reason[96];
static void note(const char*event){printf("WIFI5G t=%llu phase=%d %s\n",monotime(),trial.phase,event);fflush(stdout);}
static int failed(const char*s){snprintf(error_reason,sizeof error_reason,"%s",s);note(s);return 0;}
/* stdout stays in RAM, including early USB insertion and native helper output. */
static void export_log(void){
 if(physical_usb()!=0||!trial.report[0])return;
 int src=open("/tmp/nx-lut-lite/wifi-radio/controller.log",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(src<0)return;
 char path[640];snprintf(path,sizeof path,"%s/probe.log",trial.report);int out=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(out<0){close(src);return;}
 char b[4096];size_t total=0;ssize_t n;while(total<262144&&physical_usb()==0&&(n=read(src,b,sizeof b))>0){if(write(out,b,(size_t)n)!=n)break;total+=(size_t)n;}if(physical_usb()==0)fsync(out);close(out);close(src);
}
static int state_save(void){
 trial.magic=0x35475064;trial.heartbeat=monotime();trial.checksum=0;uint32_t h=2166136261u;const unsigned char*b=(const unsigned char*)&trial;for(unsigned i=0;i<sizeof trial;i++)h=(h^b[i])*16777619u;trial.checksum=h;
 char p[160];snprintf(p,sizeof p,TRIAL_STATE ".%ld",(long)getpid());int f=open(p,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;
 int ok=write(f,&trial,sizeof trial)==sizeof trial&&!fsync(f);if(close(f))ok=0;if(ok)ok=!rename(p,TRIAL_STATE);if(!ok)unlink(p);return ok;
}
static int state_read(void){
 struct trial t;memset(&t,0,sizeof t);struct stat st;int f=open(TRIAL_STATE,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0)return 0;
 int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&(st.st_size==(off_t)sizeof t||st.st_size==RADIO_RC614_BYTES||st.st_size==RADIO_PREVIOUS_BYTES||st.st_size==(off_t)sizeof(struct legacy_trial));size_t bytes=ok?(size_t)st.st_size:0;
 if(ok)ok=read(f,&t,bytes)==(ssize_t)bytes;close(f);if(!ok)return 0;
 int legacy=bytes==sizeof(struct legacy_trial);
 if(t.magic!=(legacy?0x35475061u:bytes==RADIO_PREVIOUS_BYTES?0x35475062u:bytes==RADIO_RC614_BYTES?0x35475063u:0x35475064u))return 0;
 uint32_t expected=t.checksum;t.checksum=0;uint32_t h=2166136261u;const unsigned char*b=(const unsigned char*)&t;for(size_t i=0;i<bytes;i++)h=(h^b[i])*16777619u;if(h!=expected)return 0;t.checksum=expected;
 if(!memchr(t.report,0,sizeof t.report)||!memchr(t.package,0,sizeof t.package)||!memchr(t.primary_reason,0,sizeof t.primary_reason)||!memchr(t.region,0,sizeof t.region)||!memchr(t.mac,0,sizeof t.mac))return 0;
 for(unsigned i=0;i<4;i++)if(!memchr(t.actors[i].path,0,sizeof t.actors[i].path))return 0;
 if(legacy){if(t.phase!=P_DONE||t.module_dirty||t.band_dirty||t.native_unconfirmed||t.child)return 0;for(unsigned i=0;i<4;i++)if(t.actors[i].frozen)return 0;}
 else{struct aba_data*a=&t.aba;if(a->state>ABA_SKIP||a->reason>ABAR_TIMEOUT||a->dirty>1||a->saved>1)return 0;if(a->dirty&&(!a->enabled||!a->saved||a->original>0xffffu||a->bmask!=(a->original&~ABA_ACTION_BIT)||!(a->original&ABA_ACTION_BIT)))return 0;}
 if(t.rvf_pid<0||t.rvf_pid==1||!!t.rvf_pid!=!!t.rvf_tick)return 0;
 if(t.ap_pending<0||t.ap_pending>1||(t.ap_pending&&(t.ap_fd<3||!t.ap_inode||!t.relay_inode)))return 0;
 trial=t;return 1;
}static int phase_save(int p){if(p>=P_SNAPSHOT&&p<P_RESTORE&&trial.guard&&proc_ticks(trial.guard)!=trial.guard_tick)return failed("WATCHDOG_IDENTITY_LOST");trial.phase=p;if(!state_save())return failed("JOURNAL_WRITE_FAILED");note("CHECKPOINT");return 1;}
static int actor_match(const struct actor*a){
 if(a->pid<=1||!a->tick||proc_ticks(a->pid)!=a->tick)return 0;char p[80],exe[PATH_MAX];snprintf(p,sizeof p,"/proc/%ld/exe",(long)a->pid);ssize_t n=readlink(p,exe,sizeof exe-1);if(n<=0)return 0;exe[n]=0;return !strcmp(exe,a->path);
}
static int radio_rvf_owner(struct rvf_owner *owner){
 return rvf_owner_read(owner)&&owner->camera==trial.camera&&owner->camera_tick==trial.camera_tick;
}
static int radio_rvf_restore_gate(void){
 if(!trial.rvf_pid)return !rvf_blocks_hotspot();
 struct rvf_owner owner;
 if(!radio_rvf_owner(&owner))return 0;
 int prep_failed=trial.phase<P_READY&&owner.phase==RVF_ENTERING&&owner.ap_up<0&&
     owner.pid==trial.rvf_pid&&owner.tick==trial.rvf_tick;
 return (owner.phase==RVF_RADIO_RESTORE||prep_failed)&&rvf_native_idle(&owner);
}
static int actor_state(pid_t p){char path[80],b[1024];snprintf(path,sizeof path,"/proc/%ld/stat",(long)p);int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return 0;ssize_t n=read(f,b,sizeof b-1);close(f);if(n<=0)return 0;b[n]=0;char*q=strrchr(b,')');return q&&q[1]==' '?q[2]:0;}
static int command(const char*const*args,unsigned budget){
 char cmd_note[320];snprintf(cmd_note,sizeof cmd_note,"COMMAND_BEGIN program=%s item=%s",args[0],args[1]?args[1]:"");note(cmd_note);
 if(trial.child)return failed("COMMAND_ALREADY_PENDING");int gate[2];if(pipe2(gate,O_CLOEXEC))return failed("COMMAND_PIPE_FAILED");pid_t p=fork();if(p<0){close(gate[0]);close(gate[1]);return failed("COMMAND_FORK_FAILED");}
 if(!p){close(gate[1]);char c;if(read(gate[0],&c,1)!=1)_exit(126);close(gate[0]);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);
  if(!strcmp(args[0],"/usr/bin/iwlist")){int out=open(TRIAL_TMP "/range.txt",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(out<0||dup2(out,1)<0)_exit(125);close(out);}
  if(!strcmp(args[0],INSTALLED "/bin/nxl-wifi-server")&&!strcmp(args[1],"--stop")){char path[160];snprintf(path,sizeof path,TRIAL_TMP "/native-stop.%ld.status",(long)getpid());int out=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(out<0||dup2(out,1)<0)_exit(125);close(out);}
  if(!strcmp(args[0],"/usr/bin/mlanutl")&&args[1]&&!strcmp(args[1],"uap0")&&args[2]&&(!strcmp(args[2],"mgmtframectrl")||!strcmp(args[2],"getdatarate")||!strcmp(args[2],"txratecfg")||!strcmp(args[2],"getlog"))){int out=open(TRIAL_TMP "/aba-cli.txt",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(out<0||dup2(out,1)<0)_exit(125);close(out);}
  execv(args[0],(char*const*)args);_exit(127);}
 close(gate[0]);if(!strcmp(args[0],INSTALLED "/bin/nxl-wifi-server")&&!strcmp(args[1],"--stop"))native_status_child=p;trial.child=p;trial.child_tick=proc_ticks(p);if(!trial.child_tick||!state_save()){kill(p,SIGKILL);close(gate[1]);waitpid(p,NULL,0);trial.child=0;return failed("COMMAND_NOT_JOURNALED");}
 int sent=write(gate[1],"x",1)==1;close(gate[1]);if(!sent)kill(p,SIGKILL);unsigned long long end=monotime()+budget;int status=0;
 for(;;){pid_t r=waitpid(p,&status,WNOHANG);if(r==p){trial.child=0;trial.child_tick=0;if(!state_save())return failed("COMMAND_RESULT_NOT_SAVED");snprintf(cmd_note,sizeof cmd_note,"COMMAND_END program=%s status=%d",args[0],status);note(cmd_note);return WIFEXITED(status)&&!WEXITSTATUS(status);}
  if(r<0)return failed("COMMAND_WAIT_FAILED");if(monotime()>=end||(trial_stop&&trial.phase<P_RESTORE))break;poll(NULL,0,25);
 }
 if(proc_ticks(p)==trial.child_tick)kill(p,SIGTERM);poll(NULL,0,100);if(waitpid(p,&status,WNOHANG)!=p){if(proc_ticks(p)==trial.child_tick)kill(p,SIGKILL);poll(NULL,0,200);if(waitpid(p,&status,WNOHANG)!=p)return failed("COMMAND_STUCK_NEEDS_BATTERY");}
 trial.child=0;trial.child_tick=0;(void)state_save();return failed("COMMAND_TIMEOUT");
}
static int read_word_text(const char*path,char*out,size_t cap){int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0)return 0;ssize_t n=read(f,out,cap-1);int ok=!close(f)&&n>0&&(size_t)n<cap-1;if(!ok)return 0;out[n]=0;out[strcspn(out,"\r\n")]=0;return 1;}
static int module_present(const char*name){FILE*f=fopen("/proc/modules","r");if(!f)return-1;char line[512],n[80];int found=0;while(fgets(line,sizeof line,f))if(sscanf(line,"%79s",n)==1&&!strcmp(n,name))found++;int ok=!ferror(f);fclose(f);return ok?found:-1;}
static int up_state(const char*n,int set,int wanted){int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return-1;struct ifreq r;memset(&r,0,sizeof r);snprintf(r.ifr_name,sizeof r.ifr_name,"%s",n);int ok=!ioctl(f,SIOCGIFFLAGS,&r),v=!!(r.ifr_flags&IFF_UP);if(ok&&set){r.ifr_flags=wanted?r.ifr_flags|IFF_UP:r.ifr_flags&~IFF_UP;ok=!ioctl(f,SIOCSIFFLAGS,&r);}close(f);return ok?v:-1;}
static int frequency_mhz(const char*n){
 struct {char name[16];union {struct{int32_t m;int16_t e;unsigned char i,flags;}freq;unsigned char pad[16];}u;}r;
 memset(&r,0,sizeof r);snprintf(r.name,sizeof r.name,"%s",n);int f=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(f<0)return 0;int ok=!ioctl(f,0x8b05,&r);close(f);if(!ok||r.u.freq.m<=0||r.u.freq.e<0||r.u.freq.e>9)return 0;
 char line[120];snprintf(line,sizeof line,"FREQUENCY_RAW m=%ld e=%d",(long)r.u.freq.m,(int)r.u.freq.e);if(trial.phase!=P_ACTIVE)note(line);
 int64_t hz=r.u.freq.m;for(int i=0;i<r.u.freq.e;i++)hz*=10;return hz>=1000000000ll&&hz<=7000000000ll&&hz%1000000==0?(int)(hz/1000000):0;
}
static int collect_actors(void){
 struct actor found[4];memset(found,0,sizeof found);DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;int ok=1;
 while((e=readdir(d))){if(strspn(e->d_name,"0123456789")!=strlen(e->d_name)||!e->d_name[0])continue;long pid=strtol(e->d_name,NULL,10);if(pid<=1||pid>INT_MAX)continue;char p[80],exe[PATH_MAX];snprintf(p,sizeof p,"/proc/%ld/exe",pid);ssize_t n=readlink(p,exe,sizeof exe-1);if(n<=0)continue;exe[n]=0;
  for(unsigned i=0;i<4;i++)if(!strcmp(exe,trial_actor_paths[i])){struct actor*a=found+i,*old=trial.actors+i;if(a->pid){ok=0;break;}a->pid=(pid_t)pid;a->tick=proc_ticks(a->pid);snprintf(a->path,sizeof a->path,"%s",exe);if(old->frozen){if(a->pid!=old->pid||a->tick!=old->tick)ok=0;else a->frozen=1;}if(!a->tick||((actor_state(a->pid)=='T'||actor_state(a->pid)=='t')&&!a->frozen))ok=0;}
  const char*leaf=strrchr(exe,'/');leaf=leaf?leaf+1:exe;int known=0;for(unsigned i=0;i<4;i++)if(!strcmp(exe,trial_actor_paths[i]))known=1;if(!known&&(!strcmp(leaf,"wpa_supplicant")||!strcmp(leaf,"connmand")||!strcmp(leaf,"net-config")||!strcmp(leaf,"mobileap-agent")||!strcmp(leaf,"wfd-manager")))ok=0;
 }
 closedir(d);for(unsigned i=0;i<4;i++)if(trial.actors[i].frozen&&(!found[i].pid||found[i].pid!=trial.actors[i].pid||found[i].tick!=trial.actors[i].tick))ok=0;
 if(!ok||!found[0].pid||!found[1].pid)return 0;memcpy(trial.actors,found,sizeof found);return 1;
}
static int freeze_actors(void){
 if(!collect_actors()||!state_save())return failed("NETWORK_ACTORS_UNAVAILABLE");
 for(unsigned i=0;i<4;i++){struct actor*a=trial.actors+i;if(!a->pid||a->frozen)continue;a->frozen=1;if(!state_save()||!actor_match(a)||kill(a->pid,SIGSTOP))return failed("NETWORK_FREEZE_FAILED");}
 unsigned long long end=monotime()+1000;for(;;){int all=1;for(unsigned i=0;i<4;i++){struct actor*a=trial.actors+i;if(a->pid&&(!actor_match(a)||actor_state(a->pid)!='T'))all=0;}if(all)return 1;if(monotime()>=end)return failed("NETWORK_FREEZE_NOT_CONFIRMED");poll(NULL,0,25);}
}
static int thaw_actors(void){int ok=1;for(unsigned i=0;i<4;i++){struct actor*a=trial.actors+i;if(!a->frozen)continue;if(!actor_match(a)){ok=0;continue;}if(kill(a->pid,SIGCONT)){ok=0;continue;}a->frozen=0;}
 unsigned long long end=monotime()+1000;for(;;){int all=1;for(unsigned i=0;i<4;i++){struct actor*a=trial.actors+i;if(a->pid&&(!actor_match(a)||actor_state(a->pid)=='T'||actor_state(a->pid)=='t'))all=0;}if(all)break;if(monotime()>=end){ok=0;break;}poll(NULL,0,25);}
 if(!state_save())ok=0;return ok;
}
static int take_capture_lock(void){if(capture_lock>=0)return 1;unsigned long long end=monotime()+3000;while((capture_lock=run_guard())<0){if(monotime()>=end)return failed("SRW_BUSY_RESTORE_HELD");poll(NULL,0,50);}return 1;}
static void release_capture_lock(void){if(capture_lock>=0)close(capture_lock);capture_lock=-1;}
static int loaders_idle(void){
 DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;int ok=1;
 while((e=readdir(d))){if(!e->d_name[0]||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;char path[96],b[2048];snprintf(path,sizeof path,"/proc/%s/cmdline",e->d_name);int f=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);if(f<0)continue;ssize_t n=read(f,b,sizeof b-1);close(f);if(n<=0)continue;b[n]=0;
  for(char*p=b;p<b+n;p+=strlen(p)+1){const char*leaf=strrchr(p,'/');leaf=leaf?leaf+1:p;if(!strcmp(leaf,"wlan.sh")||!strcmp(leaf,"insmod")||!strcmp(leaf,"rmmod")){ok=0;break;}}
 }
 closedir(d);return ok;
}
static int module_mask(void){const char*n[]={"sd8xxx","mlan","cfg80211"};int mask=0;for(unsigned i=0;i<3;i++){int v=module_present(n[i]);if(v<0||v>1)return-1;if(v)mask|=1<<i;}return mask;}
static int interface_present(const char*n){char p[96];snprintf(p,sizeof p,"/sys/class/net/%s",n);struct stat st;if(!lstat(p,&st))return 1;return errno==ENOENT?0:-1;}
static int radio_baseline(int power){
 int mask=module_mask(),sta=interface_present("mlan0"),ap=interface_present("uap0"),p2p=interface_present("p2p0");char b[180];snprintf(b,sizeof b,"RADIO_BASELINE power=%d modules=%d mlan0=%d uap0=%d p2p0=%d",power,mask,sta,ap,p2p);note(b);
 if(!loaders_idle())return failed("FOREIGN_DRIVER_COMMAND_PRESENT");
 if(p2p!=0)return failed("P2P_INTERFACE_PRESENT_OR_UNKNOWN");
 if(mask==0&&sta==0&&ap==0){trial.old_modules=0;trial.old_up=0;}
 else if(power==0&&mask==7&&sta==0&&ap==0){trial.old_modules=7;trial.old_up=0;note("RADIO_DORMANT_REGISTERED_DRIVERS native_preparation_required=1");}
 else if(power==1&&mask==7&&sta==1&&ap==0){trial.old_modules=7;trial.old_up=up_state("mlan0",0,0);if(trial.old_up<0)return failed("FACTORY_INTERFACE_UNREADABLE");char ip[32];if(interface_ip("mlan0",ip))return failed("STATION_CONNECTION_PRESENT");}
 else return failed("PARTIAL_OR_UNKNOWN_RADIO_BASELINE");
 return module_mask()==mask;
}
static int unload_driver(void){
 if(!trial.module_dirty)return failed("DRIVER_LEASE_REQUIRED");
 if(!loaders_idle())return failed("FOREIGN_DRIVER_COMMAND_PRESENT");
 const char*ifaces[]={"uap0","mlan0"};for(unsigned i=0;i<2;i++){int present=interface_present(ifaces[i]);if(present<0)return failed("INTERFACE_STATE_UNREADABLE");if(!present)continue;int up=up_state(ifaces[i],0,0);if(up<0)return failed("INTERFACE_FLAGS_UNREADABLE");char line[160];snprintf(line,sizeof line,"INTERFACE_BEFORE_UNLOAD name=%s up=%d",ifaces[i],up);note(line);if(up){const char*a[]={"/sbin/ifconfig",ifaces[i],"down",NULL};if(!command(a,4000)||up_state(ifaces[i],0,0)!=0)return failed("INTERFACE_DOWN_NOT_CONFIRMED");}note("INTERFACE_DOWN_CONFIRMED");}
 const char*names[]={"sd8xxx","mlan","cfg80211"};for(unsigned i=0;i<3;i++){if(!loaders_idle())return failed("FOREIGN_DRIVER_COMMAND_PRESENT");int p=module_present(names[i]);if(p<0)return failed("MODULE_STATE_UNREADABLE");if(p){const char*args[]={"/sbin/rmmod",names[i],NULL};if(!command(args,4000))return failed("MODULE_UNLOAD_FAILED");}if(module_present(names[i])!=0)return failed("MODULE_STILL_LOADED");}return 1;
}
static int load_driver(int probe){
 const char*region=probe?"CN":trial.region;
 const char*cfg=probe?TRIAL_TMP "/cfg80211-cn-low5g.ko":"/lib/modules/cfg80211.ko";
 if(probe&&!hot_fingerprint(cfg,RADIO_MODULE_BYTES,RADIO_MODULE_FNV))return failed("TEST_REGDB_IDENTITY_FAILED");
 char reg[32],reg2[32],mac[40];snprintf(reg,sizeof reg,"ieee80211_regdom=%s",region);snprintf(reg2,sizeof reg2,"reg_alpha2=%s",region);snprintf(mac,sizeof mac,"mac_addr=%s",trial.mac);
 const char*a[]={"/sbin/insmod",cfg,reg,NULL};const char*b[]={"/sbin/insmod","/lib/modules/mlan.ko",NULL};
 const char*c[]={"/sbin/insmod","/lib/modules/sd8xxx.ko","cfg80211_wext=15","txpwrlimit_cfg=mrvl/txpwrlimit_cfg.bin","cal_data_cfg=mrvl/WlanCalData_ext.conf","fw_name=mrvl/sd8887_uapsta.bin",reg2,mac,probe?"drv_mode=2":"drv_mode=1",probe?"uap_name=uap":NULL,probe?"max_uap_bss=1":NULL,probe?PROBE_MASK:NULL,NULL};
 if(!command(a,6000)||!command(b,6000)||!command(c,15000))return failed("MODULE_LOAD_FAILED");
 unsigned long long end=monotime()+5000;const char*iface=probe?"uap0":"mlan0";while(up_state(iface,0,0)<0){if(monotime()>=end)return failed("INTERFACE_NOT_CREATED");poll(NULL,0,100);}
 if(up_state(iface,1,probe?1:trial.old_up)<0)return failed("INTERFACE_UP_FAILED");
 return module_present("sd8xxx")==1&&module_present("mlan")==1&&module_present("cfg80211")==1;
}
static int parse_ghz(const char*p,int*used){
 if(p[0]<'2'||p[0]>'6'||p[1]!='.')return 0;int major=p[0]-'0',minor=0,n=0;const char*q=p+2;
 while(*q>='0'&&*q<='9'){if(++n>3)return 0;minor=minor*10+*q++-'0';}if(!n)return 0;while(n++<3)minor*=10;while(*q==' ')q++;if(strncmp(q,"GHz",3))return 0;*used=(int)(q+3-p);return major*1000+minor;
}
static int check_range_text(const char*b,int probe){
 if(!strstr(b,"channels in total"))return failed("CHANNEL_TABLE_MISSING");int any=0,target=0,count=0;
 for(const char*p=b;*p;p++){int used=0,mhz;if(p>b&&((p[-1]>='0'&&p[-1]<='9')||p[-1]=='.'))continue;if((mhz=parse_ghz(p,&used))>0){count++;if(mhz>=4900)any=1;if(mhz==5220||mhz==5240)target|=1<<((mhz-5220)/20);p+=used-1;}}
 return count>0&&(probe?target==3:!any);
}
static int range_table(int probe){
 const char*a[]={"/usr/bin/iwlist",probe?"uap0":"mlan0","freq",NULL};if(!command(a,3000))return failed("CHANNEL_TABLE_QUERY_FAILED");
 char b[8192];int f=open(TRIAL_TMP "/range.txt",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;ssize_t n=read(f,b,sizeof b-1);close(f);if(n<=0||(size_t)n>=sizeof b-1)return failed("CHANNEL_TABLE_INVALID");b[n]=0;
 note(probe?"PROBE_CHANNEL_TABLE":"RESTORED_CHANNEL_TABLE");fputs(b,stdout);fflush(stdout);return check_range_text(b,probe);
}
