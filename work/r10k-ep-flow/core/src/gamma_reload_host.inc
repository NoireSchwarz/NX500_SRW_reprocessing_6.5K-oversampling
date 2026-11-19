/* Persistent CAP is an intentional callback lease, not an orphaned job.
 * The same lease is reused after restore; no old executable CAP is released.
 */
#include "../../common/gamma_reload.h"
struct gamma_lease {uint32_t magic,schema,cap,index,meta[4],nonce,pid;char boot[40];uint32_t start_lo,start_hi,hash;};
static int gamma_code_guards(void){unsigned char b[2048];for(unsigned i=0;i<sizeof ng_code_guards/sizeof*ng_code_guards;i++){const struct vfw_code_guard*g=ng_code_guards+i;if(g->bytes>sizeof b||!physical_read(NULL,g->address,b,g->bytes)||vfw_hash(b,g->bytes)!=g->hash)return 0;}
 for(unsigned i=0;i<4;i++){uint32_t v[8];if(!physical_read(NULL,ng_vtable_address[i],v,32))return 0;for(unsigned j=0;j<8;j++)if(j!=ng_vtable_skip[i]&&v[j]!=ng_vtable_words[i][j])return 0;}return 1;}
static struct gamma_lease gamma_lease;
static int gamma_lease_exists,gamma_retained,gamma_requested=NG_FACTORY;
static int gamma_boot(char out[40]){int f=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_CLOEXEC);if(f<0)return 0;char b[40]={0};ssize_t n=read(f,b,39);int rc=close(f);if(rc||n!=37||b[36]!='\n')return 0;b[36]=0;for(unsigned i=0;i<36;i++)if(!((b[i]>='0'&&b[i]<='9')||(b[i]>='a'&&b[i]<='f')||b[i]=='-'))return 0;memcpy(out,b,40);return 1;}
static int gamma_cap_read(uint32_t at,void*out,unsigned n){
 if(!n||n>65536u||at<0x94000000u||(uint64_t)at+n>0xbb6cc000ULL)return 0;
 int f=open("/dev/d5_sma",O_RDONLY|O_SYNC|O_CLOEXEC);if(f<0)return 0;uint32_t page=at&~4095u;unsigned span=((at&4095u)+n+4095u)&~4095u;void*p=mmap(NULL,span,PROT_READ,MAP_SHARED,f,(off_t)(uint64_t)page);if(p==MAP_FAILED){close(f);return 0;}
 const volatile unsigned char*s=(const volatile unsigned char*)p+(at&4095u);for(unsigned i=0;i<n;i++)((unsigned char*)out)[i]=s[i];video_publish_barrier();int ok=!munmap(p,span);return !close(f)&&ok;
}
static int gamma_callback_words(uint32_t v[4]){return physical_read(NULL,NG_GAMMA_SLOT,v,4)&&physical_read(NULL,NG_PARENT_SLOT0,v+1,4)&&physical_read(NULL,NG_PARENT_SLOT1,v+2,4)&&physical_read(NULL,NG_COLOR_SLOT,v+3,4);}
static int gamma_read_state(struct ng_state*out){
 if(!gamma_lease_exists)return 0;struct ng_state again;
 for(unsigned attempt=0;attempt<6;attempt++){
  if(!gamma_cap_read(gamma_lease.cap+NG_STATE,out,sizeof*out)||!gamma_cap_read(gamma_lease.cap+NG_STATE,&again,sizeof again))return 0;
  if(!memcmp(out,&again,sizeof again)&&!(out->sequence&1u)&&!out->busy&&!out->parent_active)return out->magic==NG_MAGIC&&out->schema==NG_SCHEMA&&out->cap==gamma_lease.cap&&out->bytes==VFW_CAP_BYTES;
  pause_ms(10);
 }return 0;
}
static int gamma_process_start(uint32_t out[2]){
 char path[80],text[1024];snprintf(path,sizeof path,"/proc/%d/stat",pid);int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return 0;ssize_t n=read(f,text,sizeof text-1);int rc=close(f);if(rc||n<=0||n>=(ssize_t)(sizeof text-1))return 0;text[n]=0;char*q=strrchr(text,')');if(!q||q[1]!=' ')return 0;q+=2;
 for(unsigned field=3;field<22;field++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}
 if(*q<'0'||*q>'9')return 0;errno=0;char*end;unsigned long long ticks=strtoull(q,&end,10);if(errno||!ticks||(*end!=' '&&*end!='\n'&&*end))return 0;out[0]=(uint32_t)ticks;out[1]=(uint32_t)(ticks>>32);return 1;
}
static int gamma_load_lease(void){
 int f=open("/tmp/nx-lut-lite/gamma-r11h.lease",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(f<0){uint32_t callbacks[4];return errno==ENOENT&&gamma_callback_words(callbacks)&&callbacks[0]==NG_CALCULATE&&callbacks[1]==NG_PARENT_UPDATE&&callbacks[2]==NG_PARENT_UPDATE&&callbacks[3]==NG_COLOR_CALCULATE;}
 struct stat s;int ok=!fstat(f,&s)&&S_ISREG(s.st_mode)&&s.st_nlink==1&&s.st_size==sizeof gamma_lease&&read(f,&gamma_lease,sizeof gamma_lease)==sizeof gamma_lease;if(close(f))ok=0;
 uint32_t start[2];char boot[40];if(!ok||!gamma_boot(boot)||!gamma_process_start(start)||start[0]!=gamma_lease.start_lo||start[1]!=gamma_lease.start_hi||gamma_lease.magic!=NG_MAGIC||gamma_lease.schema!=NG_SCHEMA||gamma_lease.hash!=vfw_hash(&gamma_lease,offsetof(struct gamma_lease,hash))||memcmp(boot,gamma_lease.boot,40)||gamma_lease.pid!=(uint32_t)pid)return 0;
 if(gamma_lease.cap<0x94000000u||(gamma_lease.cap&16383u)||(uint64_t)gamma_lease.cap+VFW_CAP_BYTES>0xbb6cc000ULL||gamma_lease.index!=(gamma_lease.cap-0x94000000u)/16384u)return 0;
 uint32_t meta[4],callbacks[4];unsigned char code[0x4000];
 if(!video_entry(gamma_lease.index,meta)||memcmp(meta,gamma_lease.meta,16)||(meta[0]&255u)!=0x23u||meta[1]!=VFW_CAP_BLOCKS||!gamma_cap_read(gamma_lease.cap,code,sizeof code)||memcmp(code,vfw_code,sizeof code)||!gamma_callback_words(callbacks))return 0;
 const uint32_t stock[4]={NG_CALCULATE,NG_PARENT_UPDATE,NG_PARENT_UPDATE,NG_COLOR_CALCULATE},offset[4]={NG_COMPUTE_OFFSET,NG_PARENT_OFFSET,NG_PARENT_OFFSET,NG_COLOR_OFFSET};
 for(unsigned i=0;i<4;i++)if(callbacks[i]!=stock[i]&&callbacks[i]!=gamma_lease.cap+offset[i])return 0;
 gamma_lease_exists=1;return 1;
}
static int gamma_save_lease(void){
 if(gamma_lease_exists)return 1;memset(&gamma_lease,0,sizeof gamma_lease);gamma_lease.magic=NG_MAGIC;gamma_lease.schema=NG_SCHEMA;gamma_lease.cap=response[1];gamma_lease.index=index_owned;gamma_lease.nonce=nonce;gamma_lease.pid=(uint32_t)pid;memcpy(gamma_lease.meta,owned_meta,16);
 uint32_t start[2];if(!gamma_boot(gamma_lease.boot)||!gamma_process_start(start))return 0;gamma_lease.start_lo=start[0];gamma_lease.start_hi=start[1];gamma_lease.hash=vfw_hash(&gamma_lease,offsetof(struct gamma_lease,hash));
 char staged[120];snprintf(staged,sizeof staged,"/tmp/nx-lut-lite/gamma-r11h.lease.new.%08x",nonce);
 int f=open(staged,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;unsigned done=0;int ok=1;
 while(done<sizeof gamma_lease){ssize_t n=write(f,(unsigned char*)&gamma_lease+done,sizeof gamma_lease-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(unsigned)n;}
 if(ok&&fsync(f))ok=0;if(close(f))ok=0;if(!ok)return 0;
 /* Both module locks are held; publish a complete immutable record before
  * any RTOS callback. A partial staging file is never an active lease. */
 struct stat st;if(lstat("/tmp/nx-lut-lite/gamma-r11h.lease",&st)==0||errno!=ENOENT)return 0;
 if(rename(staged,"/tmp/nx-lut-lite/gamma-r11h.lease"))return 0;
 gamma_lease_exists=gamma_retained=1;return 1;
}
struct gamma_snapshot_record {int attempted,ok,error,state_consistent,object_available;const char*stage;uint32_t address,bytes;};
static struct gamma_snapshot_record gamma_before_snapshot={0,0,0,0,0,"NOT_ATTEMPTED",0,0},gamma_after_snapshot={0,0,0,0,0,"NOT_ATTEMPTED",0,0};
static int gamma_restore_audit_bypass;
static int gamma_audit_write(const char*dir,const char*tag,const char*leaf,const void*data,unsigned bytes){
 char p[1200];int n=snprintf(p,sizeof p,"%s/%s-%s",dir,tag,leaf);if(n<0||(unsigned)n>=sizeof p){errno=ENAMETOOLONG;return 0;}
 FILE*f=fopen(p,"wx");if(!f)return 0;int ok=fwrite(data,1,bytes,f)==bytes;if(!ok&&!errno)errno=EIO;int saved=ok?0:errno;
 if(ok&&fflush(f)){ok=0;saved=errno;}if(ok&&fsync(fileno(f))){ok=0;saved=errno;}if(fclose(f)&&ok){ok=0;saved=errno;}
 if(!ok)errno=saved?saved:EIO;return ok;
}
static int gamma_dump(const char*dir,const char*tag){
 struct gamma_snapshot_record*s=!strcmp(tag,"before")?&gamma_before_snapshot:&gamma_after_snapshot;
 memset(s,0,sizeof*s);s->attempted=1;s->stage="NO_LEASE";
 if(!gamma_lease_exists){s->ok=1;return 1;}if(!dir||!*dir){s->stage="NO_DIRECTORY";s->error=EINVAL;return 0;}
 struct ng_state g;unsigned char raw[sizeof g];uint32_t callbacks[4],ddr[216];
#define GS_READ(stage_name,at,n,expr) do{s->stage=(stage_name);s->address=(at);s->bytes=(n);errno=0;if(!(expr)){s->error=errno?errno:EIO;goto finish;}}while(0)
#define GS_WRITE(stage_name,leaf,p,n) GS_READ(stage_name,0,n,gamma_audit_write(dir,tag,leaf,p,n))
 GS_READ("READ_CAP_STATE",gamma_lease.cap+NG_STATE,sizeof raw,gamma_cap_read(gamma_lease.cap+NG_STATE,raw,sizeof raw));
 s->state_consistent=gamma_read_state(&g);if(!s->state_consistent)memcpy(&g,raw,sizeof g);
 GS_READ("READ_CALLBACKS",NG_GAMMA_SLOT,16,gamma_callback_words(callbacks));
 GS_READ("READ_DDR_BANK0",NG_DDR0,NG_BYTES,gamma_shadow_read(NG_DDR0,ddr,NG_BYTES));
 GS_READ("READ_DDR_BANK1",NG_DDR1,NG_BYTES,gamma_shadow_read(NG_DDR1,ddr+NG_WORDS,NG_BYTES));
 GS_WRITE("WRITE_LEASE","lease.bin",&gamma_lease,sizeof gamma_lease);
 GS_WRITE("WRITE_STATE","state.bin",&g,sizeof g);
 GS_WRITE("WRITE_DDR","DDR-bank0-bank1.bin",ddr,sizeof ddr);
 GS_WRITE("WRITE_CALLBACKS","callbacks.bin",callbacks,sizeof callbacks);
 char scope[1024];int n=snprintf(scope,sizeof scope,"{\"format\":\"NXL_GAMMA_VIDEO_SCOPE1\",\"mode\":%u,\"epoch\":%u,\"queued_epoch\":%u,\"video_epoch\":%u,\"video_queues\":%u,\"video_object\":%u,\"video_parent\":%u,\"video_context\":%u,\"video_driver_modes\":%u,\"video_parameter_hash\":%u,\"detached\":%u,\"fault\":%u,\"state_consistent\":%s,\"native_queue_is_hardware_completion\":false}\n",g.mode,g.epoch,g.queued_epoch,g.video_epoch,g.video_queues,g.video_object,g.video_parent,g.video_context,g.video_driver_modes,g.video_hash,g.detached,g.fault,s->state_consistent?"true":"false");
 if(n<0||(unsigned)n>=sizeof scope){s->stage="FORMAT_SCOPE";s->error=EOVERFLOW;goto finish;}
 GS_WRITE("WRITE_VIDEO_SCOPE","video-scope.json",scope,(unsigned)n);
 char technical[2048];n=snprintf(technical,sizeof technical,
  "{\"format\":\"NXL_VLOG8_REFERENCE1\",\"release\":\"R11q\",\"profile\":%u,\"encoding_bits\":8,\"signal_range\":\"full\",\"target_primaries\":\"V_GAMUT_D65\",\"native_black\":%u,\"native_white\":%u,\"scene_white_q16\":%u,\"physical_reflectance_calibrated\":false,\"reference_basis\":\"NX500_R11N_WORKING_EXPOSURE_NOT_PHYSICAL_GRAY\",\"native_working_anchor\":2965,\"working_anchor_target_code8\":132,\"explicit_working_anchor_node\":true,\"fixed_output_gain\":1.0408323021957888,\"fixed_output_offset_code8\":-0.8793405230977542,\"linear_input_gain\":2.5203589432187403,\"offline_input_scale_required\":false,\"factory_gamma_input_raw\":%u,\"factory_gamma_x_scale_raw\":%u,\"color_object\":%u,\"color_epoch\":%u,\"color_applies\":%u,\"color_reuses\":%u,\"color_failures\":%u,\"color_bank1_same\":%u,\"video_color_epoch\":%u,\"video_color_object\":%u,\"video_color_hash\":%u,\"video_queues_this_epoch\":%u,\"rollback_count\":%u,\"rollback_failed\":%u,\"transaction_fault\":%u,\"live_color_callback\":%u,\"actual_new_video_consumption_verified\":false}\n",
  g.profile,g.native_black,g.native_white,g.scene_white_q16,g.last_input,(g.last_input>>7)&8191u,g.color_object,g.color_epoch,g.color_applies,g.color_reuses,g.color_failures,g.color_bank1_same,g.video_color_epoch,g.video_color_object,g.video_color_hash,g.video_queues,g.rollback_count,g.rollback_failed,g.transaction_fault,g.live_color_callback);
 if(n<0||(unsigned)n>=sizeof technical){s->stage="FORMAT_VLOG8_SCOPE";s->error=EOVERFLOW;goto finish;}
 GS_WRITE("WRITE_VLOG8_SCOPE","vlog8-reference.json",technical,(unsigned)n);
 char binding[1200];n=snprintf(binding,sizeof binding,
  "{\"format\":\"NXL_VLOG8_BINDING1\",\"release\":\"R11q\",\"fault\":%u,\"reason\":%u,\"parent_calls\":%u,\"ignored_parents\":%u,\"ignored_parent_last\":%u,\"ignored_input_raw\":%u,\"parent\":%u,\"parent_vptr\":%u,\"input_raw\":%u,\"input_resolved\":%u,\"gamma\":%u,\"color\":%u,\"gamma_context_before\":%u,\"color_context_before\":%u,\"getter\":%u,\"getter_calls\":%u,\"native_queue_is_hardware_completion\":false}\n",
  g.fault,g.binding_reason,g.parent_calls,g.parent_ignored,g.ignored_parent_last,g.ignored_parent_input_raw,g.binding_parent,g.binding_parent_vptr,g.binding_input_raw,g.binding_input_resolved,g.binding_gamma,g.binding_color,g.binding_gamma_context_before,g.binding_color_context_before,g.binding_getter,g.binding_getter_calls);
 if(n<0||(unsigned)n>=sizeof binding){s->stage="FORMAT_BINDING";s->error=EOVERFLOW;goto finish;}
 GS_WRITE("WRITE_BINDING","binding.json",binding,(unsigned)n);
 GS_WRITE("WRITE_COLOR_FACTORY","color-factory.bin",g.color_factory,sizeof g.color_factory);
 GS_WRITE("WRITE_COLOR_TARGET","color-target.bin",g.color_target,sizeof g.color_target);
 GS_WRITE("WRITE_VIDEO_COLOR_FACTORY","video-color-factory.bin",g.video_color_factory,sizeof g.video_color_factory);
 GS_WRITE("WRITE_VIDEO_COLOR_TARGET","video-color-target.bin",g.video_color_target,sizeof g.video_color_target);
 GS_WRITE("WRITE_NATIVE_INPUTS","native-input-context.bin",g.native_context,sizeof g.native_context);
 GS_WRITE("WRITE_VIDEO_NATIVE_INPUTS","video-native-input-context.bin",g.video_native_context,sizeof g.video_native_context);

 uint32_t active_object=vfw_ram(g.applied_object,0xf74u)?g.applied_object:g.last_object;
 if(vfw_ram(active_object,0xf74u)){uint32_t object[0xf74/4];if(physical_read(NULL,active_object,object,sizeof object)){GS_WRITE("WRITE_OBJECT","object.bin",object,sizeof object);s->object_available=1;}}
 s->ok=1;s->stage="COMPLETE";s->address=s->bytes=0;
finish:
 {char report[768];int n=snprintf(report,sizeof report,"{\"format\":\"NXL_GAMMA_SNAPSHOT1\",\"ok\":%s,\"stage\":\"%s\",\"errno\":%d,\"address\":%u,\"bytes\":%u,\"state_consistent\":%s,\"object_available\":%s,\"shadow_capture_atomic\":false,\"native_queue_is_hardware_completion\":false}\n",s->ok?"true":"false",s->stage,s->error,s->address,s->bytes,s->state_consistent?"true":"false",s->object_available?"true":"false");
  if(n<0||(unsigned)n>=sizeof report||!gamma_audit_write(dir,tag,"snapshot.json",report,(unsigned)n)){if(s->ok){s->ok=0;s->error=errno?errno:EIO;s->stage="WRITE_SNAPSHOT_REPORT";s->address=s->bytes=0;}}
 }
#undef GS_READ
#undef GS_WRITE
 return s->ok;
}
static int gamma_before_ready(const char*dir,int read_only){
 if(gamma_dump(dir,"before"))return 1;
 if(read_only||gamma_requested!=NG_OFF)return 0;
 gamma_restore_audit_bypass=1;return 1;
}
static int gamma_result(int ok,int state,const char*dir){
 (void)state;if(dir&&*dir)(void)gamma_dump(dir,"after");struct ng_state g;memset(&g,0,sizeof g);int valid=gamma_lease_exists&&gamma_read_state(&g);const char*phase=!gamma_lease_exists?"OFF":!valid?"PREPARED":g.fault?"FAILED":g.queued_epoch!=g.epoch?"WAIT_NATIVE":g.detached?"FACTORY_QUEUED":"NATIVE_QUEUED";
 const char*mode=valid&&g.mode==NG_LOG?"LOG":valid&&g.mode==NG_LIFT?"LIFT":"FACTORY";
 if(valid&&g.fault)ok=0;
 const char*why=!ok?(valid&&g.fault?"GAMMA_NATIVE_FAILURE":reason):!gamma_lease_exists?"GAMMA_FACTORY_NO_LEASE":!valid?"GAMMA_PREPARED_NOT_ENTERED":g.fault?"GAMMA_NATIVE_FAILURE":g.queued_epoch!=g.epoch?"GAMMA_WAIT_NATIVE_UPDATE":g.detached?"GAMMA_FACTORY_NATIVE_QUEUED":"GAMMA_NATIVE_QUEUED_HARDWARE_PENDING";
 printf("NXL_GAMMA_RELOAD1\nmode=%s\nphase=%s\nreason=%s\nepoch=%u\nqueued_epoch=%u\nqualified=%u\napplies=%u\ninvalidations=%u\nqueues=%u\nfault=%u\nend=NXL_GAMMA_RELOAD_END1\n",mode,phase,why,g.epoch,g.queued_epoch,g.qualified,g.applies,g.invalidations,g.queues,g.fault);
 if(dir&&*dir){char p[1200];snprintf(p,sizeof p,"%s/result.json",dir);FILE*f=fopen(p,"wx");if(f){fprintf(f,"{\"format\":\"NXL_GAMMA_RELOAD1\",\"ok\":%s,\"reason\":\"%s\",\"phase\":\"%s\",\"mode\":\"%s\",\"lease_cap\":%u,\"lease_bytes\":%u,\"new_lease_retained\":%u,\"transient_CAP_return_verified\":%u,\"RTOS_entered\":%u,\"RTOS_retired\":%u,\"UI_thawed\":%u,\"native_control_status\":%u,\"before_snapshot_ok\":%s,\"before_snapshot_stage\":\"%s\",\"after_snapshot_ok\":%s,\"after_snapshot_stage\":\"%s\",\"restore_before_snapshot_bypassed\":%s,\"native_queue_is_hardware_completion\":false,\"hardware_pixels_verified\":false}\n",ok?"true":"false",why,phase,mode,gamma_lease_exists?gamma_lease.cap:0,gamma_lease_exists?VFW_CAP_BYTES:0,gamma_retained,cap_released,rtos_entered,rtos_retired,ui_thawed,receipt.reserved[0],gamma_before_snapshot.ok?"true":"false",gamma_before_snapshot.stage,gamma_after_snapshot.ok?"true":"false",gamma_after_snapshot.stage,gamma_restore_audit_bypass?"true":"false");fclose(f);}}
 return ok;
}
