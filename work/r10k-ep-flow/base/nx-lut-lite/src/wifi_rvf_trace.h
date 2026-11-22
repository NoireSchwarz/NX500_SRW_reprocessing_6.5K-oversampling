/* SPDX-License-Identifier: AGPL-3.0-only. Coarse failure evidence, never hot MMIO. */
#include "wifi_rvf_auth_contract.h"
#define RVF_FAILURE WIFI_TMP "/wifi-rvf-failure-rc6121"
struct rvf_failure {uint32_t magic,checksum;int32_t pid,http,error,poll_events;unsigned long long tick,updated,sent,received;char phase[48];};
static struct rvf_failure rvf_rpc;
static unsigned long long rvf_rpc_begin;
static const char*rvf_rpc_phase;
static int rvf_ram_region(pid_t pid,uint32_t ptr,size_t bytes,int execute,char*description,size_t cap){
 if(ptr<0x10000u||bytes>256||ptr>0xff000000u-bytes)return 0;char path[80],line[512];snprintf(path,sizeof path,"/proc/%ld/maps",(long)pid);FILE*f=fopen(path,"r");if(!f)return 0;int found=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,off;char flags[8],dev[24],name[300]="";unsigned long inode;int n=sscanf(line,"%x-%x %7s %x %23s %lu %299s",&lo,&hi,flags,&off,dev,&inode,name);if(n<6||lo>ptr||(uint64_t)ptr+bytes>hi)continue;
  int good=execute==1?flags[0]=='r'&&flags[2]=='x'&&name[0]=='/':execute==2?!strcmp(flags,"rw-p")&&(!name[0]||!strcmp(name,HOT_APP)||!strcmp(name,"/usr/lib/libdi-network-dlna-api.so")):!strcmp(flags,"rw-p")&&(!strcmp(name,"[heap]")||!name[0]);if(!good)continue;
  if(description&&cap)snprintf(description,cap,"lo=%08x offset=%08x flags=%s file=%s",lo,off,flags,name);found++;
 }int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;return ok;
}
static void rvf_auth_snapshot(const char*phase){
 pid_t pid=rvf.camera;unsigned long long tick=rvf.camera_tick;char path[80],line[512];if(pid<=1||!tick||proc_ticks(pid)!=tick)return;
 snprintf(path,sizeof path,"/proc/%ld/maps",(long)pid);FILE*f=fopen(path,"r");if(!f)return;unsigned count=0;uint32_t base=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,off;char flags[8],dev[24],name[300];unsigned long inode;if(sscanf(line,"%x-%x %7s %x %23s %lu %299s",&lo,&hi,flags,&off,dev,&inode,name)!=7)continue;
  if(!strcmp(name,"/usr/lib/libdi-network-dlna-api.so")&&!strcmp(flags,"r-xp")&&!off&&lo>=0x10000u&&hi>lo&&hi-lo>=RVF_AUTH_FUNCTION+sizeof rvf_auth_code){base=lo;count++;}}
 int ok=!ferror(f)&&count==1&&base<0xff000000u-RVF_AUTH_GLOBAL_END;if(fclose(f))ok=0;if(!ok){fprintf(stderr,"RVF_AUTH phase=%s result=LIBRARY_MAPPING_UNAVAILABLE\n",phase);return;}
 snprintf(path,sizeof path,"/proc/%ld/mem",(long)pid);int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return;unsigned char code[sizeof rvf_auth_code];uint32_t a[2],b[2];
 ok=hot_read(fd,base+RVF_AUTH_FUNCTION,code,sizeof code)&&!memcmp(code,rvf_auth_code,sizeof code)&&hot_read(fd,base+RVF_AUTH_CALLBACK,a,4)&&hot_read(fd,base+RVF_AUTH_OBJECT,a+1,4)&&hot_read(fd,base+RVF_AUTH_CALLBACK,b,4)&&hot_read(fd,base+RVF_AUTH_OBJECT,b+1,4)&&!memcmp(a,b,sizeof a)&&proc_ticks(pid)==tick;
 if(ok){uint32_t addresses[2],after[2],values[2],again[2];int resolved=hot_read(fd,base+RVF_AUTH_CALLBACK_GOT,addresses,4)&&hot_read(fd,base+RVF_AUTH_OBJECT_GOT,addresses+1,4)&&rvf_ram_region(pid,addresses[0],4,2,NULL,0)&&rvf_ram_region(pid,addresses[1],4,2,NULL,0)&&hot_read(fd,addresses[0],values,4)&&hot_read(fd,addresses[1],values+1,4)&&hot_read(fd,base+RVF_AUTH_CALLBACK_GOT,after,4)&&hot_read(fd,base+RVF_AUTH_OBJECT_GOT,after+1,4)&&!memcmp(addresses,after,sizeof addresses)&&hot_read(fd,addresses[0],again,4)&&hot_read(fd,addresses[1],again+1,4)&&!memcmp(values,again,sizeof values)&&proc_ticks(pid)==tick;
  fprintf(stderr,"RVF_AUTH phase=%s direct_callback=%08x direct_object=%08x GOT_resolved=%d\n",phase,a[0],a[1],resolved);if(resolved){a[0]=values[0];a[1]=values[1];}
  char target[384]="unresolved";int executable=a[0]&&rvf_ram_region(pid,a[0]&~1u,4,1,target,sizeof target);fprintf(stderr,"RVF_AUTH phase=%s pid=%ld tick=%llu library_base=%08x symbol_source=%s callback=%08x executable=%d %s object=%08x\n",phase,(long)pid,tick,base,resolved?"LOADED_GOT":"DIRECT_UNCONFIRMED",a[0],executable,target,a[1]);
  if(a[1]&&!(a[1]&3u)&&rvf_ram_region(pid,a[1],0x38,0,NULL,0)){uint32_t x[2],y[2],object,reference;
   if(hot_read(fd,a[1]+0x24,x,4)&&hot_read(fd,a[1]+0x34,x+1,4)&&hot_read(fd,a[1]+0x24,y,4)&&hot_read(fd,a[1]+0x34,y+1,4)&&!memcmp(x,y,sizeof x)&&(!resolved||(hot_read(fd,base+RVF_AUTH_OBJECT_GOT,&reference,4)&&reference==addresses[1]))&&hot_read(fd,resolved?addresses[1]:base+RVF_AUTH_OBJECT,&object,4)&&object==a[1]&&proc_ticks(pid)==tick)fprintf(stderr,"RVF_AUTH phase=%s object_callback=%08x connection_state=%u\n",phase,x[0],x[1]);
  }
 }else fprintf(stderr,"RVF_AUTH phase=%s result=CODE_OR_DOUBLE_READ_MISMATCH\n",phase);close(fd);fflush(stderr);
}
static void rvf_rpc_reset(void){memset(&rvf_rpc,0,sizeof rvf_rpc);rvf_rpc.pid=rvf.pid;rvf_rpc.tick=rvf.tick;rvf_rpc_begin=monotime();rvf_rpc_phase="LISTENER_CHECK";}
static int rvf_rpc_error(const char*phase,int error){rvf_rpc_phase=phase;rvf_rpc.error=error;return 0;}
static void rvf_rpc_hex(const char*name,const void*p,size_t n){const unsigned char*b=p;fprintf(stderr,"RVF_RPC_%s bytes=%zu hex=",name,n);for(size_t i=0;i<n&&i<1024;i++)fprintf(stderr,"%02x",b[i]);fputc('\n',stderr);}
static int rvf_rpc_result(int status,const char*response,size_t received){
 rvf_rpc.http=status;rvf_rpc.received=received;rvf_rpc.updated=monotime();snprintf(rvf_rpc.phase,sizeof rvf_rpc.phase,"%s",status?"HTTP_RESPONSE":rvf_rpc_phase?rvf_rpc_phase:"UNKNOWN");
 fprintf(stderr,"RVF_RPC phase=%s http=%d errno=%d poll_events=%x sent=%llu received=%llu elapsed_ms=%llu\n",rvf_rpc.phase,status,rvf_rpc.error,(unsigned)rvf_rpc.poll_events,rvf_rpc.sent,rvf_rpc.received,rvf_rpc.updated-rvf_rpc_begin);
 if(status!=200){if(received)rvf_rpc_hex("RAW_RESPONSE",response,received);struct rvf_failure old;
  if(rvf_rpc.pid>1&&rvf_rpc.tick&&(!wire_read(RVF_FAILURE,&old,sizeof old,0x52463121u)||old.pid!=rvf_rpc.pid||old.tick!=rvf_rpc.tick)){
   (void)wire_write(RVF_FAILURE,&rvf_rpc,sizeof rvf_rpc,0x52463121u);rvf_auth_snapshot("FIRST_RPC_FAILURE");
  }}fflush(stderr);return status;
}
/* Copy the small final record and bounded log once, after native cleanup.
 * The SD remains untouched while USB is active. Unique names preserve history. */
static int rvf_export_file(const char*dir,const char*source,const char*leaf,size_t limit){
 if(physical_usb()!=0)return 0;int in=open(source,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);if(in<0)return 0;struct stat st;
 if(fstat(in,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>limit){close(in);return 0;}char path[256];snprintf(path,sizeof path,"%s/%s",dir,leaf);int out=open(path,O_CREAT|O_EXCL|O_WRONLY|O_CLOEXEC|O_NOFOLLOW,0600);if(out<0){close(in);return 0;}unsigned char buf[4096];ssize_t n=0;int ok=1;size_t total=0;
 while(physical_usb()==0&&(n=read(in,buf,sizeof buf))>0){size_t at=0;while(at<(size_t)n){ssize_t k=write(out,buf+at,(size_t)n-at);if(k<=0){ok=0;break;}at+=(size_t)k;}if(!ok)break;total+=at;}
 if(n<0||total!=(size_t)st.st_size||physical_usb()!=0)ok=0;if(ok&&fsync(out))ok=0;if(close(out))ok=0;if(close(in))ok=0;return ok;
}
static void rvf_export_result(void){
 if(physical_usb()!=0)return;char dir[192];unsigned sequence=0;for(;sequence<100;sequence++){snprintf(dir,sizeof dir,"/mnt/mmc/NXDEMO51/WIFI_RVF_%llu_%ld_%u",rvf.started,(long)rvf.pid,sequence);if(!mkdir(dir,0700))break;if(errno!=EEXIST)return;}if(sequence==100)return;
 fflush(stderr);int state_saved=rvf_export_file(dir,RVF_STATE,"rvf-state.bin",4096);int failure_saved=rvf_export_file(dir,RVF_FAILURE,"first-failure.bin",4096);int log_saved=rvf_export_file(dir,WIFI_TMP "/wifi-rvf.log","rpc.log",262144);if(physical_usb()!=0)return;
 char path[240];snprintf(path,sizeof path,"%s/result.txt",dir);int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(fd<0)return;
 dprintf(fd,"version=" WIFI_VERSION "\nstate=%d\nrequests=%u\ndirty=%d\nrestored=%d\nap_restored=%d\nreason=%s\nframes=%llu\nstate_saved=%d\nfirst_failure_saved=%d\nrpc_log_saved=%d\nend=NXL_RVF_RESULT1\n",rvf.state,rvf.requests,rvf.dirty,rvf.restored,rvf.ap_restored,rvf.reason,rvf.frames,state_saved,failure_saved,log_saved);if(physical_usb()==0)(void)fsync(fd);close(fd);
}
