/* SPDX-License-Identifier: AGPL-3.0-only. One-shot owned-AP management forwarding A/B/A.
 * The only experiment SET is mgmtframectrl(original & ~0x2000), then original.
 * Existing P4 driver/channel/power/rate and camera controls are unchanged. */
#define ABA_CLI TRIAL_TMP "/aba-cli.txt"
static int aba_trace=-1;
static unsigned long long aba_publish_next;
static void aba_publication(void){
 struct aba_public p;memset(&p,0,sizeof p);struct aba_data*a=&trial.aba;p.pid=getpid();p.tick=proc_ticks(getpid());p.updated=monotime();p.state=(int)a->state;p.restored=a->restored;p.changed=a->changed;p.reason=a->reason;
 if(a->phase_start&&p.updated>=a->phase_start)p.elapsed=p.updated-a->phase_start;
 for(unsigned i=0;i<3;i++){const struct aba_stage*s=a->stages+i;p.body[i]=s->body1>=s->body0?s->body1-s->body0:0;p.net[i]=s->net1>=s->net0?s->net1-s->net0:0;p.stage_valid[i]=s->valid;}
 (void)wire_write(ABA_PUBLIC,&p,sizeof p,0x41423611u);aba_publish_next=p.updated+1000;
}
static int aba_text(const char*path,char*b,size_t cap){int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0)return 0;struct stat st;int ok=cap>1&&!fstat(f,&st)&&S_ISREG(st.st_mode);ssize_t n=ok?read(f,b,cap-1):-1;char extra;ssize_t tail=n>=0?read(f,&extra,1):-1;int rc=close(f);if(n<0||tail!=0||rc)return 0;b[n]=0;return memchr(b,0,(size_t)n)==NULL;}
static int aba_number(const char*path,unsigned long long*out){char b[64],*end;if(!aba_text(path,b,sizeof b)||b[0]<'0'||b[0]>'9')return 0;errno=0;unsigned long long n=strtoull(b,&end,10);if(errno||end==b)return 0;while(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')end++;if(*end)return 0;*out=n;return 1;}
static int aba_exe(pid_t p,unsigned long long t,const char*path){return wire_process(p,t,path);}
static int aba_host(pid_t*out,unsigned long long*tick){DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;pid_t found=0;unsigned long long stamp=0;
 while((e=readdir(d))){if(!e->d_name[0]||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;long n=strtol(e->d_name,NULL,10);if(n<=1||n>INT_MAX)continue;unsigned long long t=proc_ticks((pid_t)n);if(aba_exe((pid_t)n,t,"/usr/sbin/hostapd")){if(found){found=0;break;}found=(pid_t)n;stamp=t;}}
 closedir(d);if(!found)return 0;*out=found;*tick=stamp;return 1;
}
static int aba_identity(int restore){
 struct aba_data*a=&trial.aba;struct hot_state s;unsigned long long index;
 if(!supported()||!hot_same_epoch(&s)||!s.owned||!s.ready||s.phase!=2||interface_present("p2p0")!=0||module_mask()!=7||frequency_mhz("uap0")!=5180)return 0;
 if(!aba_number("/sys/class/net/uap0/ifindex",&index)||!index||index>INT_MAX||(a->ifindex&&index!=a->ifindex))return 0;
 if(!hot_fingerprint("/usr/sbin/hostapd",ABA_HOST_BYTES,ABA_HOST_FNV)||!hot_fingerprint("/usr/bin/mlanutl",ABA_CLI_BYTES,ABA_CLI_FNV)||!hot_fingerprint("/lib/modules/sd8xxx.ko",575964u,0x140ae8edu))return 0;
 if(!a->host){if(restore||!aba_host(&a->host,&a->host_tick))return 0;a->ifindex=(unsigned)index;}
 if(!aba_exe(a->host,a->host_tick,"/usr/sbin/hostapd"))return 0;
 return restore||aba_exe(a->server,a->server_tick,INSTALLED "/bin/nxl-wifi-server");
}
static int aba_counters(struct transfer_wire*t,unsigned long long*net){
 unsigned long long now=monotime();if(!wire_read(ABA_TRANSFER,t,sizeof*t,0x54523611u)||now<t->updated||now-t->updated>3000||!aba_exe(t->pid,t->tick,INSTALLED "/bin/nxl-wifi-server"))return 0;
 struct aba_data*a=&trial.aba;if(a->server&&(a->server!=t->pid||a->server_tick!=t->tick))return 0;
 if(!aba_number("/sys/class/net/uap0/statistics/tx_bytes",net))return 0;
 if(!a->server){a->server=t->pid;a->server_tick=t->tick;}return 1;
}
static int aba_cli(const char*verb,int set,unsigned mask,char*out,size_t cap){
 if(strcmp(verb,"mgmtframectrl")&&strcmp(verb,"getdatarate")&&strcmp(verb,"txratecfg")&&strcmp(verb,"getlog"))return 0;
 if(set&&(strcmp(verb,"mgmtframectrl")||!trial.aba.saved||!trial.aba.dirty||(mask!=trial.aba.original&&mask!=trial.aba.bmask)))return 0;
 char value[24];snprintf(value,sizeof value,"0x%x",mask);const char*args[]={"/usr/bin/mlanutl","uap0",verb,set?value:NULL,NULL};
 int ok=command(args,2500);if(!aba_text(ABA_CLI,out,cap))return 0;
 if(aba_trace>=0)dprintf(aba_trace,"QUERY %s set=%d status=%d time_ms=%llu\n%s\nQUERY_END\n",verb,set,ok,monotime(),out);
 if(!ok||strstr(out,"mlanutl:")||strstr(out,"Invalid number")||strstr(out,"Error:"))return 0;return 1;
}
static int aba_mask(unsigned*out){char b[8192],extra;unsigned n;if(!aba_cli("mgmtframectrl",0,0,b,sizeof b)||sscanf(b,"Registed Management Frame Mask: 0x%x %c",&n,&extra)!=1||n>0xffffu)return 0;*out=n;return 1;}
static int aba_auto(void){char b[8192];return aba_cli("txratecfg",0,0,b,sizeof b)&&strstr(b,"Tx Rate Configuration:")&&strstr(b,"0xFF (Auto)");}
static int aba_write_mask(unsigned value){char b[8192];return aba_cli("mgmtframectrl",1,value,b,sizeof b);}
static void aba_read_file(int out,const char*path,int config){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);dprintf(out,"FILE_BEGIN %s\n",path);if(f<0){dprintf(out,"UNAVAILABLE errno=%d\nFILE_END\n",errno);return;}
 struct stat st;char b[32769];int ok=!fstat(f,&st)&&S_ISREG(st.st_mode);off_t at=ok&&st.st_size>32768?st.st_size-32768:0;ssize_t n=ok?pread(f,b,32768,at):-1;close(f);if(n<0||memchr(b,0,n<0?0:(size_t)n)){dprintf(out,"READ_UNAVAILABLE\nFILE_END\n");return;}b[n]=0;
 if(config){char*save=NULL;for(char*p=strtok_r(b,"\n",&save);p;p=strtok_r(NULL,"\n",&save)){if(!strncmp(p,"channel=",8)||!strncmp(p,"hw_mode=",8)||!strncmp(p,"ieee80211",9)||!strncmp(p,"ht_capab=",9)||!strncmp(p,"vht_",4)||!strncmp(p,"wmm_",4)||!strncmp(p,"country_code=",13))dprintf(out,"%s\n",p);}}
 else{size_t off=0;while(off<(size_t)n){ssize_t k=write(out,b+off,(size_t)n-off);if(k<=0)break;off+=(size_t)k;}}
 dprintf(out,"\nFILE_END bytes=%ld tail=%lld\n",(long)n,(long long)at);
}
static int aba_snapshot(const char*name){
 char path[200];snprintf(path,sizeof path,TRIAL_TMP "/aba-%s.txt",name);int f=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;
 struct transfer_wire t;unsigned long long net;int coverage=aba_counters(&t,&net);dprintf(f,"format=NXL_ABA_SNAPSHOT1\nname=%s\nstate=%u\ntime_ms=%llu\ncounters_valid=%d\nbody_bytes=%llu\nnet_tx_bytes=%llu\n",name,trial.aba.state,monotime(),coverage,coverage?t.body:0,coverage?net:0);
 aba_trace=f;char b[8192];unsigned mask=0;unsigned expected=trial.aba.state==ABA_B?trial.aba.bmask:trial.aba.original;int ok=aba_mask(&mask)&&mask==expected&&aba_auto();dprintf(f,"mask_valid=%d\nmask=0x%x\nexpected_mask=0x%x\n",ok,mask,expected);
 if(!aba_cli("getdatarate",0,0,b,sizeof b))ok=0;
 if(!aba_cli("getlog",0,0,b,sizeof b))ok=0;aba_trace=-1;
 const char*paths[]={"/opt/var/lib/misc/hostapd.conf","/var/log/hostapd.log","/proc/mwlan/uap0/info","/proc/mwlan/uap0/debug","/proc/net/arp","/proc/net/dev","/proc/net/snmp","/proc/net/netstat","/proc/net/tcp","/proc/sys/net/ipv4/tcp_wmem","/proc/sys/net/ipv4/tcp_rmem"};
 for(unsigned i=0;i<sizeof paths/sizeof*paths;i++)aba_read_file(f,paths[i],i==0);
 dprintf(f,"end=NXL_ABA_SNAPSHOT_END1\n");if(fsync(f))ok=0;if(close(f))ok=0;return ok&&coverage;
}
static int aba_original_file(void){
 struct aba_data*a=&trial.aba;if(physical_usb()!=0)return 0;char path[640];snprintf(path,sizeof path,"%s/mgmt-original.txt",trial.report);int f=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(f<0)return 0;
 int ok=dprintf(f,"format=NXL_MGMT_ORIGINAL1\nversion=0.4.0-RC6.11\ncamera=%ld\ncamera_tick=%llu\nhostapd=%ld\nhostapd_tick=%llu\nserver=%ld\nserver_tick=%llu\nifindex=%u\noriginal_mask=0x%x\nB_mask=0x%x\nchanged_bit=13\nfrequency_mhz=5180\nrate_auto=1\nend=NXL_MGMT_ORIGINAL_END1\n",(long)trial.camera,trial.camera_tick,(long)a->host,a->host_tick,(long)a->server,a->server_tick,a->ifindex,a->original,a->bmask)>0&&!fsync(f);if(close(f))ok=0;return ok;
}
static int aba_restore(void){
 struct aba_data*a=&trial.aba;if(!a->dirty)return 1;if(!a->saved||!aba_identity(1))return 0;unsigned current;if(!aba_mask(&current))return 0;
 if(current!=a->original){if(current!=a->bmask){a->reason=ABAR_FOREIGN_MASK;return 0;}if(!aba_write_mask(a->original)||!aba_mask(&current)||current!=a->original)return 0;}
 a->dirty=0;a->restored=1;return state_save();
}
static int aba_abort(unsigned reason){
 struct aba_data*a=&trial.aba;if(!a->reason)a->reason=reason;int ok=aba_restore();a->state=ok?ABA_ABORT:ABA_HOLD;if(!ok&&!a->reason)a->reason=ABAR_RESTORE;(void)state_save();aba_publication();return ok;
}
static int aba_claim(void){
 struct aba_data*a=&trial.aba;char text[80];struct stat st;int f=open(ABA_ARM,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(f<0){if(errno==ENOENT){aba_publication();return 1;}a->state=ABA_SKIP;a->reason=ABAR_ARM;aba_publication();return 1;}
 int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)strlen(ABA_ARM_TEXT)&&read(f,text,sizeof text)==(ssize_t)strlen(ABA_ARM_TEXT);close(f);if(ok)ok=!memcmp(text,ABA_ARM_TEXT,strlen(ABA_ARM_TEXT));
 if(!ok){a->state=ABA_SKIP;a->reason=ABAR_ARM;aba_publication();return 1;}
 a->enabled=1;a->state=ABA_WAIT_AP;a->deadline=monotime()+180000;if(!state_save())return 0;char to[240];snprintf(to,sizeof to,"%s.claimed-%ld-%llu",ABA_ARM,(long)trial.owner,trial.owner_tick);struct stat now;
 if(physical_usb()!=0||lstat(ABA_ARM,&now)||now.st_ino!=st.st_ino||now.st_dev!=st.st_dev||!lstat(to,&now)||errno!=ENOENT||rename(ABA_ARM,to)){a->enabled=0;a->state=ABA_SKIP;a->reason=ABAR_ARM;}
 if(!state_save())return 0;aba_publication();return 1;
}
static int aba_begin(unsigned state,const struct transfer_wire*t,unsigned long long net,unsigned long long now){
 struct aba_data*a=&trial.aba;unsigned index=state-ABA_A1;if(index>2)return 0;struct aba_stage*s=a->stages+index;memset(s,0,sizeof*s);s->start=now;s->body0=s->body1=t->body;s->net0=s->net1=net;s->frames0=s->frames1=t->frames;a->phase_start=now;a->last_sample=now;a->last_body=t->body;a->last_net=net;a->state=state;return state_save();
}
static int aba_change(void){
 struct aba_data*a=&trial.aba;unsigned current;if(!aba_identity(0)||!aba_auto()||!aba_mask(&current)||current!=a->original||!a->saved||!a->sd_saved)return 0;
 a->dirty=1;if(!state_save())return 0;if(!aba_write_mask(a->bmask)||!aba_mask(&current)||current!=a->bmask||!aba_auto())return 0;a->changed=1;return state_save();
}
static int aba_tick(void){
 struct aba_data*a=&trial.aba;unsigned long long now=monotime();if(now>=aba_publish_next)aba_publication();if(!a->enabled||a->state>=ABA_DONE)return a->state!=ABA_HOLD;
 if(now>a->deadline)return aba_abort(ABAR_TIMEOUT);
 struct transfer_wire t;unsigned long long net;if(!aba_counters(&t,&net)){if(a->state==ABA_WAIT_AP)return 1;return aba_abort(ABAR_COUNTER);}
 if(a->state==ABA_WAIT_AP){if(!aba_identity(0))return aba_abort(ABAR_IDENTITY);if(!aba_auto())return aba_abort(ABAR_AUTO);unsigned original;if(!aba_mask(&original))return aba_abort(ABAR_QUERY);a->original=original;a->bmask=original&~ABA_ACTION_BIT;a->saved=1;
  if(!(original&ABA_ACTION_BIT)){a->state=ABA_SKIP;a->reason=ABAR_NO_ACTION;if(!state_save())return 0;aba_publication();return 1;}
  a->trigger_body=t.body;a->trigger_net=net;a->state=ABA_WAIT_DATA;a->deadline=now+180000;return state_save();
 }
 if(now>a->deadline)return aba_abort(ABAR_TIMEOUT);
 if(t.body<a->last_body||net<a->last_net)return aba_abort(ABAR_COUNTER);
 if(a->state==ABA_WAIT_DATA){if(t.body<a->trigger_body||net<a->trigger_net)return aba_abort(ABAR_COUNTER);if(t.body-a->trigger_body<262144||net-a->trigger_net<262144)return 1;
  if(!aba_identity(0)||!aba_snapshot("A1-start"))return aba_abort(ABAR_QUERY);a->deadline=monotime()+120000;if(!aba_counters(&t,&net))return aba_abort(ABAR_COUNTER);return aba_begin(ABA_A1,&t,net,monotime());
 }
 unsigned index=a->state-ABA_A1;if(index>2)return aba_abort(ABAR_QUERY);struct aba_stage*s=a->stages+index;s->body1=t.body;s->net1=net;s->frames1=t.frames;
 if(now-a->last_sample>=1000){if(t.body>a->last_body&&net>a->last_net)s->samples++;a->last_sample=now;a->last_body=t.body;a->last_net=net;}
 unsigned long long elapsed=now-s->start;if(elapsed<20000)return 1;
 int coverage=s->body1-s->body0>=1048576&&s->net1-s->net0>=1048576&&s->samples>=10;
 if(!coverage){if(elapsed<40000)return 1;return aba_abort(ABAR_COVERAGE);}
 if(!aba_identity(0)||!aba_auto())return aba_abort(ABAR_IDENTITY);s->end=now;s->valid=1;if(!state_save())return aba_abort(ABAR_ORIGINAL);
 const char*names[]={"A1-end","B-end","A2-end"};if(!aba_snapshot(names[index])){s->valid=0;return aba_abort(ABAR_QUERY);}
 if(index==0){if(!aba_original_file())return aba_abort(ABAR_ORIGINAL);a->sd_saved=1;if(!state_save()||!aba_change())return aba_abort(ABAR_QUERY);}
 else if(index==1){if(!aba_restore())return aba_abort(ABAR_RESTORE);}
 else{unsigned value;if(a->dirty||!a->restored||!aba_mask(&value)||value!=a->original)return aba_abort(ABAR_RESTORE);a->state=ABA_DONE;if(!state_save())return 0;aba_publication();note("MGMT_ABA_COMPLETE_ORIGINAL_READBACK_VERIFIED");return 1;}
 if(!aba_counters(&t,&net))return aba_abort(ABAR_COUNTER);return aba_begin(ABA_A1+index+1,&t,net,monotime());
}
static int aba_finish(void){if(!trial.aba.enabled)return 1;if(trial.aba.state==ABA_DONE&&!trial.aba.dirty)return 1;return aba_abort(ABAR_STOP);}
static void aba_teardown(int radio_ok){
 struct aba_data*a=&trial.aba;if(!a->enabled||!a->dirty)return;
 if(radio_ok&&trial.phase==P_DONE&&!trial.module_dirty&&!trial.band_dirty&&!trial.native_unconfirmed&&interface_present("uap0")==0){a->dirty=0;a->reset_cleanup=1;a->state=ABA_ABORT;if(!state_save()){a->dirty=1;a->state=ABA_HOLD;a->reason=ABAR_RESTORE;}note("MGMT_ABA_PARAMETER_LIFETIME_ENDED_WITH_VERIFIED_DRIVER_RESTORE no_original_readback_claim=1");}
 else{a->state=ABA_HOLD;a->reason=ABAR_RESTORE;(void)state_save();}aba_publication();
}
