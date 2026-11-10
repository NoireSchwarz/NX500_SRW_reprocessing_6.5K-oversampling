/* SPDX-License-Identifier: AGPL-3.0-only. Read-only proof only: no allocator,
 * native entry, ptrace, process stop/resume, MMIO write or hardware unlock. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include "sha256.h"
#include "../../common/progress_emit.h"
#include "oem_ro_read.h"
#include "session_rearm_contract.h"
#include "session_rearm_backend.h"
#include "shell_idle.h"
#include "../../hardware/src/ep_kernel_guards.h"
#include "memory_guards.h"
#include "alloc_guards.h"
#include "bridge_guards.h"
#include "front_readiness.h"
#include "rearm_front.h"
#include "../../post/src/post_json.h"
#include "../../common/shared_workspace_io.h"
#include "../../common/workflow_hold.h"
#include "../../common/physical_ro.h"
#include "retired_shadow.h"
#ifndef ROOT
#define ROOT "/mnt/mmc/NXDEMO51"
#endif
#define VERSION "NXL_SESSION_REARM=0.4.0-demo51-422; READONLY_REARM_R7O; RESOURCE_FACTS_PROOF3; DIAGNOSTIC_RECEIVE_SEMANTICS; RETIREMENT_BEFORE_INITIALIZATION; OPERATION_PROGRESS3; NO_CAP_RELEASE"
static const char *job,*previous,*token,*output,*reason="ARGUMENTS_INVALID";
static char front[1100];static uint32_t old_pa,old_va,old_nonce,old_bytes,backend_pa,backend_va,backend_attempt;
static unsigned authenticated,metadata_ok,code_ok,snapshots,old_free,roots_clear,parked,queue_empty,workers_idle;
static unsigned backend_free,backend_retired,native_resolved,category_failures,backend_samples,backend_metadata,mmio_reads,read_faults,read_failures,last_address;
static struct shell_idle_report shell_report;
static unsigned render_initialization_required,preview_retirement_samples;
static unsigned latched_jpeg_reference_samples,latched_jpeg_reference_mask;
static struct r7_allocation_lifetime lifetime;
static unsigned front_allocation_retired,backend_allocation_retired;
static unsigned allocator_final_revalidated;
static int devfd=-1,procfd=-1,camera_pid,supervisor;static uint32_t cap_base,shell_base,libc_base;static FILE *logf;
static void reject(unsigned bit,const char*r){category_failures|=bit;if(!reason||!strcmp(reason,"ARGUMENTS_INVALID")||!strcmp(reason,"NOT_STARTED"))reason=r;if(logf){fprintf(logf,"CATEGORY_FAIL mask=%u reason=%s\n",bit,r);fflush(logf);}}
static void hexhash(const void *p,size_t n,char h[65]){unsigned char b[32];digest_bytes(p,n,b);digest_hex(b,h);}
static int safe(const char*s){if(!s||s[0]!='/'||strlen(s)>1000||strstr(s,"..")||strstr(s,"//"))return 0;return strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_-.")==strlen(s);}
static int jobpath(const char*s){return safe(s)&&!strncmp(s,ROOT"/JOB_",strlen(ROOT"/JOB_"))&&!strchr(s+strlen(ROOT)+1,'/');}
static int no_links(const char *s){if(!safe(s))return 0;char b[1200];strcpy(b,s);for(char*p=b+1;;p++){if(*p&&*p!='/')continue;char c=*p;*p=0;struct stat st;if(lstat(b,&st))return 0;if(S_ISLNK(st.st_mode)){char v[128];ssize_t n=readlink(b,v,sizeof v-1);if(strcmp(b,"/mnt/mmc")||n<0)return 0;v[n]=0;if(strcmp(v,"/opt/storage/sdcard"))return 0;struct stat t;const char*q[]={"/opt","/opt/storage","/opt/storage/sdcard"};for(unsigned i=0;i<3;i++)if(lstat(q[i],&t)||!S_ISDIR(t.st_mode))return 0;}else if(!S_ISDIR(st.st_mode))return 0;*p=c;if(!c)return 1;}}
static int readtext(const char*p,char*b,size_t cap){int f=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;struct stat st;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&(uint64_t)st.st_size<cap;if(ok){ssize_t n=read(f,b,cap-1);ok=n==st.st_size;if(ok)b[n]=0;}if(close(f))ok=0;return ok;}
static int line(const char*p,char*b,size_t n){FILE*f=fopen(p,"r");if(!f)return 0;int ok=fgets(b,(int)n,f)!=NULL;fclose(f);if(ok)b[strcspn(b,"\r\n")]=0;return ok;}
static int save(const char*n,const void*b,size_t z){char p[1300];if(snprintf(p,sizeof p,"%s/%s",output,n)>=(int)sizeof p)return 0;int f=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;size_t at=0;int ok=1;while(at<z){ssize_t w=write(f,(const char*)b+at,z-at);if(w<=0){ok=0;break;}at+=(size_t)w;}if(ok&&fsync(f))ok=0;if(close(f))ok=0;return ok;}
#include "reset_decision_recorder.h"
static int key(const char*b,const char*k,char*out,size_t n){size_t kl=strlen(k);unsigned found=0;for(const char*p=b;*p;){const char*e=strchr(p,'\n');if(!e)return 0;if((size_t)(e-p)>kl&&p[kl]=='='&&!memcmp(p,k,kl)){size_t z=(size_t)(e-p)-kl-1;if(++found!=1||z>=n)return 0;memcpy(out,p+kl+1,z);out[z]=0;}p=e+1;}return found==1;}
static int num(const char*b,const char*k,uint32_t*out){char v[80],*e;if(!key(b,k,v,sizeof v)||!v[0]||strspn(v,"0123456789")!=strlen(v))return 0;unsigned long long n=strtoull(v,&e,10);if(*e||n>UINT32_MAX)return 0;*out=(uint32_t)n;return 1;}
static int filehash(const char*p,char h[65]){int f=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;struct stat st;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode);struct sha256 c;sha256_init(&c);unsigned char b[8192];ssize_t n=0;if(ok)while((n=read(f,b,sizeof b))>0)sha256_update(&c,b,(size_t)n);if(n<0)ok=0;if(close(f))ok=0;if(ok){unsigned char d[32];sha256_final(&c,d);digest_hex(d,h);}return ok;}
static int boundfile(const char*base,const char*leaf,const char*want){char p[1300],h[65];return strlen(want)==64&&snprintf(p,sizeof p,"%s/%s",base,leaf)<(int)sizeof p&&filehash(p,h)&&!strcmp(h,want);}
static int jnum(const char*b,const char*k,uint32_t*out){struct p49_json j={b,b+strlen(b),0};p49_jws(&j);if(j.p==j.end||*j.p++!='{')return 0;unsigned found=0;for(;;){char n[96];p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}')break;if(!p49_jstring(&j,n,sizeof n))return 0;p49_jws(&j);if(j.p==j.end||*j.p++!=':')return 0;p49_jws(&j);const char*s=j.p;if(!p49_jvalue(&j))return 0;if(!strcmp(n,k)){if(found++)return 0;uint64_t v=0;for(const char*p=s;p<j.p;p++){if(*p<'0'||*p>'9')return 0;v=v*10+(unsigned)(*p-'0');if(v>UINT32_MAX)return 0;}*out=(uint32_t)v;}p49_jws(&j);if(j.p==j.end)return 0;if(*j.p=='}')break;if(*j.p++!=',')return 0;}return found==1;}
static int journal(void){char path[1300];snprintf(path,sizeof path,"%s/STATE.journal",previous);char*b=malloc(131073);if(!b)return 0;int ok=readtext(path,b,131073);uint32_t seq=0;char*last=NULL;if(ok)for(char*p=b;*p;){char*e=strstr(p,"end=NX51END1\n");if(!e){ok=0;break;}e+=13;char*sum=strstr(p,"sha256=");if(!sum||sum>=e){ok=0;break;}char want[65],h[65];if(e-sum!=85||memcmp(p,"NX51STATE1\n",11)){ok=0;break;}memcpy(want,sum+7,64);want[64]=0;hexhash(p,(size_t)(sum-p),h);if(strcmp(want,h)||sum[71]!='\n'){ok=0;break;}char c=*e;*e=0;uint32_t n=0;char v[1100];if(!num(p,"seq",&n)||!n||(seq&&n!=seq+1)||!key(p,"job",v,sizeof v)||strcmp(v,previous)){ok=0;*e=c;break;}seq=n;last=p;*e=c;if(!*e)break;p=e;}
 if(ok&&last){char state[64],verified[16];ok=num(last,"backend_attempt",&backend_attempt)&&backend_attempt>0&&key(last,"state",state,sizeof state)&&!strcmp(state,"COMPLETE")&&key(last,"front_verified",verified,sizeof verified)&&!strcmp(verified,"1")&&key(last,"front",front,sizeof front)&&safe(front)&&!strncmp(front,previous,strlen(previous))&&front[strlen(previous)]=='/'&&strstr(front+strlen(previous),"/front_")==front+strlen(previous)&&strlen(front)>6&&!strcmp(front+strlen(front)-6,"/front")&&no_links(front);}else ok=0;free(b);return ok;}
#include "fast_rearm_metadata.inc"
static int metadata(void){if(!journal())return 0;if(fast_rearm_metadata())return 1;char p[1300],manifest[4096],post[4096],result[32768],v[128],rh[65],ph[65],ch[65];snprintf(p,sizeof p,"%s/P49_FRONT_COMPLETE",front);if(!readtext(p,manifest,sizeof manifest)||strncmp(manifest,"NXP49FRONTCOMPLETE1\n",20)||!key(manifest,"result_sha256",rh,sizeof rh)||!key(manifest,"post_input_sha256",ph,sizeof ph)||!boundfile(front,"result.json",rh)||!boundfile(front,"case_00/post-input.txt",ph))return 0;snprintf(p,sizeof p,"%s/case_00/post-input.txt",front);if(!readtext(p,post,sizeof post)||strncmp(post,"NXP49POSTINPUT1\n",16)||!key(post,"front_result_sha256",ch,sizeof ch)||!boundfile(front,"case_00/result.json",ch))return 0;
 char src[65],y[65],uv[65];uint32_t nonce,width,height,stride;if(!key(post,"source_srw_sha256",src,sizeof src)||!key(post,"y_sha256",y,sizeof y)||!key(post,"uv_sha256",uv,sizeof uv)||!num(post,"nonce",&nonce)||!num(post,"width",&width)||!num(post,"height",&height)||!num(post,"stride",&stride)||width!=6480||height!=4320||stride!=6496||!key(post,"native_frame_complete",v,sizeof v)||strcmp(v,"1"))return 0;
 snprintf(p,sizeof p,"%s/case_00/result.json",front);struct p49_front_expected exp={src,y,uv,width,height,stride};if(!readtext(p,result,sizeof result)||!p49_front_json(result,strlen(result),nonce,0,&exp)||!jnum(result,"cap_allocation_bytes",&old_bytes)||!r7_set_front_capacity(old_bytes)||!jnum(result,"pa",&old_pa)||!jnum(result,"va",&old_va)||!r7_extent(old_pa,old_va)||!jnum(result,"nonce",&old_nonce)||nonce!=old_nonce)return 0;
 const char*ones[]={"hook_restored","hook_native_restore_verified","original_code_restored","detached","idle_registers_verified","camera_resumed_verified","liveness_after","ownership_verified"};for(unsigned i=0;i<sizeof ones/sizeof *ones;i++){uint32_t n;if(!jnum(result,ones[i],&n)||n!=1)return 0;}const char*zeros[]={"api_inflight_at_exit","camera_stopped_at_exit"};for(unsigned i=0;i<2;i++){uint32_t n;if(!jnum(result,zeros[i],&n)||n)return 0;}return 1;}
static int backend_info(void){char dir[1300],p[1400],b[16384],outcome[2048],hash[65];snprintf(dir,sizeof dir,"%s/jpeg/attempt_%u/hardware",previous,backend_attempt);if(!no_links(dir))return 0;snprintf(p,sizeof p,"%s/controller-outcome.txt",dir);if(!readtext(p,outcome,sizeof outcome)||!key(outcome,"result_sha256",hash,sizeof hash)||!boundfile(dir,"result.json",hash))return 0;snprintf(p,sizeof p,"%s/result.json",dir);uint32_t bytes,index;return readtext(p,b,sizeof b)&&jnum(b,"pa",&backend_pa)&&jnum(b,"va",&backend_va)&&jnum(b,"requested_bytes",&bytes)&&r7_set_backend_capacity(bytes)&&jnum(b,"allocator_index",&index)&&backend_pa>=0x94000000u&&!(backend_pa&16383u)&&backend_va&&!(backend_va&16383u)&&(uint64_t)backend_pa+bytes<=0xc0000000ULL&&(uint64_t)backend_va+bytes<=0x100000000ULL&&index==((backend_pa-0x94000000u)>>14)&&index<=R7_BLOCKS-r7_backend_bytes/16384u;}
#include "resource_metadata.inc"
static int historical_no_native_locks(void){const char *names[]={"engine.lock","trace.lock"};for(unsigned i=0;i<2;i++){char p[1300],b[4096];snprintf(p,sizeof p,"/tmp/nx-lut-lite/%s",names[i]);struct stat st;if(lstat(p,&st)){if(errno==ENOENT)continue;reason="NATIVE_LOCK_INSPECTION_FAILED";return 0;}if(!S_ISDIR(st.st_mode)||!no_links(p)){reason="FOREIGN_NATIVE_LOCK_INVALID";return 0;}char f[1400],name[80];snprintf(f,sizeof f,"%s/NX51_OWNER",p);int own=readtext(f,b,sizeof b);if(own){snprintf(name,sizeof name,"%s-owner.txt",names[i]);if(!save(name,b,strlen(b)))return 0;}char match[1200];snprintf(match,sizeof match,"job=%s\n",previous);reason=own&&strstr(b,match)?"INHERITED_NATIVE_HOLD_PRESENT":"FOREIGN_NATIVE_HOLD_PRESENT";const char*leaves[]={"pid","RESTART_REQUIRED"};for(unsigned k=0;k<2;k++){snprintf(f,sizeof f,"%s/%s",p,leaves[k]);if(readtext(f,b,sizeof b)){snprintf(name,sizeof name,"%s-%s.txt",names[i],leaves[k]);if(!save(name,b,strlen(b)))return 0;}}return 0;}return 1;}
static int no_native_locks(void){
 const char*names[]={"engine.lock","trace.lock","memory-test.lock","demo50-recovery.lock"};
 for(unsigned i=0;i<4;i++){char path[1200];struct wh_hold h;snprintf(path,sizeof path,"/tmp/nx-lut-lite/%s",names[i]);
  if(!wh_check(path,previous,&h)){reason="FOREIGN_OR_LIVE_WORKFLOW_HOLD";return 0;}
  if(h.present){char text[1300],name[80];int n=snprintf(text,sizeof text,"job=%s\nboot=%s\nreason=%s\nround=%u\n",h.job,h.boot,h.reason,h.round);snprintf(name,sizeof name,"%s-preserved.txt",names[i]);if(n<=0||n>=(int)sizeof text||!save(name,text,(size_t)n))return 0;}
 }return 1;
}
static int identity(int pid,int*parent,uint64_t*ticks){char p[80],b[2048];snprintf(p,sizeof p,"/proc/%d/stat",pid);if(!line(p,b,sizeof b))return 0;char*q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;for(unsigned f=3;f<=22;f++){if(!*q)return 0;if(f==3){q++;continue;}while(*q==' ')q++;char*e;unsigned long long v=strtoull(q,&e,10);if(e==q)return 0;if(f==4)*parent=(int)v;if(f==22){*ticks=v;return v>0;}q=e;}return 0;}
static int auth(void){const char*j=getenv("NX51_JOB"),*t=getenv("NX51_SESSION_TOKEN"),*pr=getenv("NX51_PREVIOUS_JOB"),*mode=getenv("NX51_REARM_ONLY");if(!j||strcmp(j,job)||!t||strcmp(t,token)||!pr||strcmp(pr,previous)||!mode||strcmp(mode,"1"))return 0;int pp=0,gp=0;uint64_t pt=0,gt=0;if(!identity(getppid(),&pp,&pt)||!identity(pp,&gp,&gt))return 0;char b[1024],want[1024];if(!readtext("/tmp/nx-lut-lite/NX51.active.lease",b,sizeof b))return 0;snprintf(want,sizeof want,"pid=%d\nstart_ticks=%llu\ntoken=%s\nmode=rearm\nend=NX51LEASE2\n",pp,(unsigned long long)gt,token);if(strcmp(b,want))return 0;int f=open(ROOT"/run.lock",O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;struct stat st;int ok=!fstat(f,&st)&&S_ISREG(st.st_mode);int locked=flock(f,LOCK_EX|LOCK_NB),busy=locked<0&&(errno==EWOULDBLOCK||errno==EAGAIN);if(!locked)flock(f,LOCK_UN);close(f);supervisor=pp;return ok&&busy;}
static int firmware(void){char a[64],b[64];FILE*f=fopen("/etc/version.info","r");if(!f)return 0;int ok=fgets(a,sizeof a,f)&&fgets(b,sizeof b,f);fclose(f);if(!ok)return 0;a[strcspn(a,"\r\n")]=0;b[strcspn(b,"\r\n")]=0;return !strcmp(a,"1.12")&&!strcmp(b,"NX500");}
static int fingerprint(const char*p,size_t want,uint32_t hash){FILE*f=fopen(p,"rb");if(!f)return 0;unsigned char b[4096];size_t n,total=0;uint32_t h=2166136261u;while((n=fread(b,1,sizeof b,f))){total+=n;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;}int ok=!ferror(f)&&total==want&&h==hash;fclose(f);return ok;}
static int procread(uint32_t a,void*b,size_t n){return procfd>=0&&pread(procfd,b,n,(off_t)(uint64_t)a)==(ssize_t)n;}
static int codeguards(uint32_t base,const struct alloc_guard*g,unsigned n){uint32_t b[8];for(unsigned i=0;i<n;i++)if(!procread(base+g[i].offset,b,32)||memcmp(b,g[i].words,32))return 0;return 1;}
static int process_setup(void){DIR*d=opendir("/proc");if(!d)return 0;struct dirent*e;unsigned found=0;while((e=readdir(d))){int pid=atoi(e->d_name);if(pid<=1||pid==getpid())continue;char p[128],b[80];snprintf(p,sizeof p,"/proc/%d/comm",pid);if(!line(p,b,sizeof b))continue;if(!strcmp(b,"di-camera-app")){camera_pid=pid;found++;}if(r7_native_worker_name(b)){closedir(d);reason="OTHER_NATIVE_WORKER_PRESENT";return 0;}if(!strcmp(b,"nxl-session.arm")&&pid!=supervisor){closedir(d);return 0;}}closedir(d);if(found!=1)return 0;
 char p[128],b[768];snprintf(p,sizeof p,"/proc/%d/maps",camera_pid);FILE*f=fopen(p,"r");if(!f)return 0;while(fgets(b,sizeof b,f)){unsigned lo,hi,off;char perms[8],name[256];if(sscanf(b,"%x-%x %7s %x %*s %*u %255s",&lo,&hi,perms,&off,name)!=5||off||strcmp(perms,"r-xp"))continue;if(!strcmp(name,"/usr/lib/libcapture-fw-prod.so")){if(cap_base){fclose(f);return 0;}cap_base=lo;}if(!strcmp(name,"/usr/lib/libshell-command.so")){if(shell_base){fclose(f);return 0;}shell_base=lo;}if(!strcmp(name,"/lib/libc-2.13.so")){if(libc_base){fclose(f);return 0;}libc_base=lo;}}fclose(f);if(!cap_base||!shell_base||!libc_base)return 0;snprintf(p,sizeof p,"/proc/%d/mem",camera_pid);procfd=open(p,O_RDONLY|O_CLOEXEC);if(procfd<0)return 0;
 uint32_t w[8];for(unsigned i=0;i<MEM_GUARD_COUNT;i++)if(!procread(mem_guards[i].address,w,32)||memcmp(w,mem_guards[i].words,32))return 0;if(!codeguards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof *alloc_cap_guards)||!codeguards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof *alloc_shell_guards)||!codeguards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof *alloc_libc_guards))return 0;
 if(!fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV)||!fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)||!fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)||!fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV))return 0;uint32_t dispatch=0;if(!procread(shell_base+0x1e33cu+15u*8u+4u,&dispatch,4)||dispatch!=shell_base+0xc6c8u){reason="APP_DISPATCH_NOT_ORIGINAL";return 0;}workers_idle=1;return 1;}
static int shell_queue(void *x,int *out){
 (void)x;int q=msgget(0x8828,0);struct msqid_ds ds;
 if(q<0||msgctl(q,IPC_STAT,&ds)||ds.msg_qnum||ds.msg_perm.uid!=getuid())return 0;
 *out=q;return 1;
}
static int shell_worker(void *x,unsigned *named,int *tid){
 (void)x;char p[128],b[80];snprintf(p,sizeof p,"/proc/%d/task",camera_pid);
 DIR*d=opendir(p);if(!d)return 0;struct dirent*e;unsigned scanned=0;int ok=1;*named=0;*tid=0;
 while((e=readdir(d))){
  int t=atoi(e->d_name);if(t<=1)continue;
  if(++scanned>512){ok=0;break;}
  snprintf(p,sizeof p,"/proc/%d/task/%d/comm",camera_pid,t);
  if(!line(p,b,sizeof b)){ok=0;break;}
  if(!strcmp(b,"shell_di_app")){(*named)++;*tid=t;}
 }
 if(closedir(d))ok=0;return ok;
}
static int shell_line(void*x,int tid,const char*leaf,char*b,size_t n){
 (void)x;char p[160];snprintf(p,sizeof p,"/proc/%d/task/%d/%s",camera_pid,tid,leaf);return line(p,b,n);
}
static int shell_word(void*x,uint32_t a,uint32_t*out){(void)x;return procread(a,out,4);}
static void shell_note(void*x,int tid,const char*leaf,const char*b){
 (void)x;if(logf){fprintf(logf,"SHELL_OBSERVATION tid=%d leaf=%s raw=%s\n",tid,leaf,b);fflush(logf);}
}
static uint64_t shell_clock(void*x){
 (void)x;struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000)return UINT64_MAX;
 return (uint64_t)t.tv_sec*1000u+(unsigned)t.tv_nsec/1000000u;
}
static int shell_pause(void*x,unsigned ms){
 (void)x;struct timespec t={ms/1000u,(long)(ms%1000u)*1000000};return nanosleep(&t,NULL)==0;
}
#include "shell_receive_context.inc"
static const struct shell_idle_ops shell_ops={shell_queue,shell_worker,shell_line,shell_word,shell_note,shell_clock,shell_pause,shell_receive_context};
static int shell_check(unsigned wait){
 int ok=wait?shell_idle_wait(NULL,&shell_ops,libc_base,shell_base,&shell_report):shell_idle_once(NULL,&shell_ops,libc_base,shell_base,&shell_report);
 parked=shell_report.parked;queue_empty=shell_report.queue_empty;
 if(logf){fprintf(logf,"SHELL_PROOF ok=%d reason=%s named=%u tid=%d queue=%d syscall=%llu samples=%u consecutive=%u elapsed_ms=%u\n",ok,shell_report.reason,shell_report.named,shell_report.tid,shell_report.queue,(unsigned long long)shell_report.frame.number,shell_report.samples,shell_report.consecutive,shell_report.elapsed_ms);fflush(logf);}
 return ok;
}
static int shellproof(void){return shell_check(0);}
static int ram(uint32_t a,void*b,unsigned n){unsigned char*out=b;while(n){unsigned z=4096-(a&4095u);if(z>n)z=n;if(!oem_ram_window(a,z))return 0;last_address=a;if(logf){fprintf(logf,"READ_BEGIN address=0x%08x bytes=%u\n",a,z);fflush(logf);}void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)(a&~4095u));if(p==MAP_FAILED){read_failures++;return 0;}struct oem_ro_fault fault={0};int ok=oem_ro_guarded_load((volatile unsigned char*)p+(a&4095u),out,z,1,&fault);if(munmap(p,4096))ok=0;if(!ok){read_failures++;if(fault.signal)read_faults++;if(logf){fprintf(logf,"READ_FAILED address=0x%08x signal=%d code=%d offset=%u errno=%d\n",a,fault.signal,fault.code,fault.offset,errno);fflush(logf);}return 0;}out+=z;a+=z;n-=z;}return 1;}
static int fr_adapter(void*x,uint32_t a,uint32_t*out,unsigned n){(void)x;return ram(a,out,n*4);}
static int physical_guards(void){uint32_t b[8];for(unsigned i=0;i<sizeof p33_rtos_guards/sizeof *p33_rtos_guards;i++)if(!ram(p33_rtos_guards[i].offset,b,32)||memcmp(b,p33_rtos_guards[i].words,32))return 0;return 1;}
static struct rs_snapshot shadow_current,shadow_first;
static unsigned shadow_observations,shadow_current_idle,shadow_latched_references,shadow_check_failed;
static int shadow_checkpoint(const struct fr_snapshot*fr,unsigned pass){
 memset(&shadow_current,0,sizeof shadow_current);shadow_current_idle=0;
 if(!inventory.count)return 1;
 unsigned char code[132];
 if(ram(0x801c9c50u,code,sizeof code)&&ram(0x8100f7b0u,shadow_current.slot,8)){
  shadow_current.dispatch_hash=sw_checksum(code,sizeof code);shadow_current.read_mask|=1;
 }
 if(ram(FR_TASK+4u,shadow_current.callbacks,160)&&ram(FR_TASK+0x644u,shadow_current.callbacks+40,160))shadow_current.read_mask|=2;
 if(ram(0x817d7b48u,shadow_current.shadow,72)&&ram(0x817d7b90u,shadow_current.shadow+18,72)&&ram(FR_TASK+3388u,shadow_current.shadow+36,24))shadow_current.read_mask|=4;
 static const uint32_t dma[]={0x8142a6f0u,0x8142a780u,0x20810308u,0x20810310u,0x20810314u,0x20810380u,0x20810384u,0x20810388u,0x20810390u,0x20810408u,0x2081040cu,0x20810418u,0x2081045cu,0x20810468u};
 struct pr_reader reader={.fd=devfd};int readable=1;
 for(unsigned i=0;i<14;i++)if(!pr_read(&reader,dma[i],shadow_current.dma+i,4))readable=0;
 pr_close(&reader);if(readable)shadow_current.read_mask|=8;
 shadow_current_idle=(unsigned)rs_idle(fr,&shadow_current,&inventory);
 if(!shadow_observations)shadow_first=shadow_current;
 else if(!rs_stable(&shadow_first,&shadow_current))shadow_check_failed=1;
 shadow_observations++;
 char name[80];snprintf(name,sizeof name,"shadow-observation-%u.bin",pass);if(!save(name,&shadow_current,sizeof shadow_current))return 0;
 decision_event("{\"event\":\"retained_descriptor_observation\",\"pass\":%u,\"read_mask\":%u,\"current_idle_proof\":%u,\"stable\":%u,\"raw_sample\":\"%s\"}\n",pass,shadow_current.read_mask,shadow_current_idle,!shadow_check_failed,name);
 return 1;
}
static int rootwindow(uint32_t a,unsigned n){
 uint32_t b[128];if(!n||n>sizeof b||n&3||!ram(a,b,n))return 0;
 for(unsigned i=0;i<n/4;i++){
  if(logf)fprintf(logf,"ROOT_WORD address=0x%08x value=0x%08x kind=RTOS_IDENTITY_PA\n",a+i*4,b[i]);
  if(ri_stale_reference(&inventory,b[i])){
   if(shadow_current_idle&&rs_descriptor_word(a+i*4,&shadow_current)){
    shadow_latched_references++;if(logf)fprintf(logf,"LATCHED_DESCRIPTOR_REFERENCE address=0x%08x value=0x%08x current_still_dma_idle=1 allocator_retired=1\n",a+i*4,b[i]);continue;
   }
   if(logf)fprintf(logf,"OLD_PHYSICAL_ROOT address=0x%08x value=0x%08x kind=RTOS_IDENTITY_PA\n",a+i*4,b[i]);return 0;
  }
 }return 1;
}
static int pool(uint32_t*t,unsigned pass){uint32_t v=0;if(!ram(0x817dc498u,&v,4)||v!=0x80574230u||!ram(0x817dc49cu,&v,4)||v!=0x805742acu||!ram(0x817dc4bcu,&v,4)||v!=0x81dfc800u||!ram(0x81dfc800u,&v,4)||v!=0x80574b60u||!ram(0x81dfc904u,&v,4)||v!=0x80574c48u||!ram(0x81eac908u,&v,4)||v!=0x94000000u||!ram(0x81eac90cu,&v,4)||v!=R7_BLOCKS)return 0;uint32_t before,after;if(!ram(0x81eac910u,&before,4)||before>R7_BLOCKS||!ram(0x81dfc908u,t,R7_BLOCKS*16u)||!ram(0x81eac910u,&after,4)||before!=after)return 0;char name[64];snprintf(name,sizeof name,"allocator-%u.bin",pass);if(!save(name,t,R7_BLOCKS*16u))return 0;int layout=ri_partition(&inventory,t,before);int retired=layout&&ri_retired(&inventory);old_free=backend_free=retired;
 memset(&lifetime,0,sizeof lifetime);lifetime.valid=layout;lifetime.free_blocks=inventory.free_blocks;lifetime.head_count=inventory.heads;
 lifetime.front_state=lifetime.backend_state=retired?R7_RANGE_VACANT:R7_RANGE_SAME_SHAPE;
 if(layout)memcpy(lifetime.occupied,inventory.occupied,sizeof lifetime.occupied);
 for(unsigned i=0;i<inventory.count;i++)if(inventory.item[i].state==RI_REUSED&&retired)lifetime.front_state=lifetime.backend_state=R7_RANGE_REUSED;
 if(logf)fprintf(logf,"RESOURCE_INVENTORY pass=%u operations=%u resolved=%u ranges=%u partition=%d retired=%d\n",pass,inventory.operations,inventory.resolved,inventory.count,layout,retired);
 decision_allocator(t,pass);return layout&&!decision_failed;}
/* Private exact, read-only device whitelist. Every device access is one
 * aligned guarded 32-bit load; gated JPEG is never dereferenced. */
static int mmio_allowed(uint32_t a){if(a==0x20821020u||a==0x20821024u||a==0x20821118u||a==0x20821218u)return 1;for(unsigned i=0;i<5;i++)if(a==0x20821070u+i*4||a==0x208210c8u+i*4)return 1;for(unsigned i=0;i<32;i++)if(a==0x20820000u+r7_dma_offsets[i])return 1;for(unsigned i=0;i<12;i++)if(a==0x2082a000u+r7_jpeg_offsets[i])return 1;return 0;}
static int mmio(uint32_t a,uint32_t*out){if(!mmio_allowed(a)||a&3)return 0;last_address=a;void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)(a&~4095u));if(p==MAP_FAILED){read_failures++;return 0;}struct oem_ro_fault f={0};if(logf){fprintf(logf,"MMIO_READ_BEGIN address=0x%08x width=4 bytes=4\n",a);fflush(logf);}int ok=oem_ro_guarded_load((volatile unsigned char*)p+(a&4095u),out,4,4,&f);mmio_reads++;if(munmap(p,4096))ok=0;if(!ok){read_failures++;if(f.signal)read_faults++;if(logf)fprintf(logf,"MMIO_READ_FAILED address=0x%08x signal=%d code=%d offset=%u\n",a,f.signal,f.code,f.offset);}else if(logf)fprintf(logf,"MMIO_READ_END address=0x%08x value=0x%08x\n",a,*out);if(logf)fflush(logf);return ok;}
static int backend_guards(void){uint32_t b[8];for(unsigned i=0;i<sizeof ep_kernel_guards/sizeof *ep_kernel_guards;i++)if(!ram(ep_kernel_guards[i].va-EP_KERNEL_DELTA,b,32)||memcmp(b,ep_kernel_guards[i].words,32))return 0;return 1;}
static void backend_take(struct r7_backend_sample*s){memset(s,0,sizeof *s);s->kernel_readable=ram(EP_KERNEL_STATE_VA-EP_KERNEL_DELTA,s->kernel,sizeof s->kernel)&&ram(EP_KERNEL_MUTEX_VA-EP_KERNEL_DELTA,s->mutex,sizeof s->mutex);s->rtos_readable=ram(0x817d8aacu,s->rtos,sizeof s->rtos)&&ram(0x817d8c98u,&s->owner,4);s->top_readable=1;for(unsigned i=0;i<2;i++)if(!mmio(0x20821020u+i*4,s->clock+i))s->top_readable=0;for(unsigned i=0;i<5;i++){if(!mmio(0x20821070u+i*4,s->mask+i))s->top_readable=0;if(!mmio(0x208210c8u+i*4,s->raw+i))s->top_readable=0;}for(unsigned i=0;i<2;i++)if(!mmio(i?0x20821218u:0x20821118u,s->otf+i))s->top_readable=0;for(unsigned i=0;i<32;i++)if(!mmio(0x20820000u+r7_dma_offsets[i],s->dma+i))s->top_readable=0;/* A clock failure or gated clock skips the JPEG bank entirely. */if(s->top_readable&&!(s->clock[0]&0x80u)&&!(s->clock[1]&0x1e0000u)){s->jpeg_readable=1;for(unsigned i=0;i<12;i++)if(!mmio(0x2082a000u+r7_jpeg_offsets[i],s->jpeg+i))s->jpeg_readable=0;}}
static int backend_proof(void){if(!backend_guards()){reject(64,"BACKEND_KERNEL_CODE_UNPROVEN");return 0;}struct r7_backend_sample first,now;unsigned failures=0;for(unsigned i=0;i<R7_BACKEND_SAMPLES;i++){if(!(i%4)){char phase[32],detail[100];snprintf(phase,sizeof phase,"CHECK_BACKEND%u",i/4+1);snprintf(detail,sizeof detail,"Checking DMA, queues and owner: sample %u/16",i+1);nxl_live_progress(phase,"RUNNING",detail);}backend_take(&now);if(r7_backend_background_activity(&now))preview_retirement_samples++;unsigned latched=r7_backend_latched_jpeg_refs(&now,old_pa,backend_pa);if(latched){latched_jpeg_reference_samples++;latched_jpeg_reference_mask|=latched;}char name[80];snprintf(name,sizeof name,"backend-sample-%02u.bin",i);if(!save(name,&now,sizeof now)){reject(128,"BACKEND_DIAGNOSTIC_SAVE_FAILED");failures++;}const char*e=r7_backend_retirement_error(&now);if(!e)for(unsigned j=0;j<32;j++)if(ri_stale_reference(&inventory,now.dma[j])){e="BACKEND_DMA_UNRETIRED_RESOURCE_REFERENCE";break;}decision_backend(&now,i,e,!i||r7_backend_retirement_stable(&first,&now));if(e){reject(64,e);failures++;}if(!i)first=now;else if(!r7_backend_retirement_stable(&first,&now)){reject(64,"BACKEND_STATE_CHANGED_DURING_QUIET_WINDOW");failures++;}backend_samples++;if(i+1<R7_BACKEND_SAMPLES){struct timespec t={0,20000000};if(nanosleep(&t,NULL)){reject(64,"BACKEND_SAMPLE_SLEEP_FAILED");failures++;}}}return !failures&&backend_samples==R7_BACKEND_SAMPLES;}
static int resources(void){uint32_t*first=malloc(R7_BLOCKS*16u),*next=malloc(R7_BLOCKS*16u);if(!first||!next){free(first);free(next);reason="SNAPSHOT_MEMORY_UNAVAILABLE";return 0;}unsigned failures=0,first_valid=0,front_all=1,back_all=1,roots_all=1,front_free_all=1,back_free_all=1;for(unsigned k=0;k<3;k++){char phase[32],detail[100];snprintf(phase,sizeof phase,"CHECK_POOL%u",k+1);snprintf(detail,sizeof detail,"Checking allocator and Still: sample %u/3",k+1);nxl_live_progress(phase,"RUNNING",detail);uint32_t*t=k?next:first;int valid=pool(t,k);if(!valid){reject(1,"ALLOCATOR_SNAPSHOT_UNREADABLE_OR_UNSTABLE");failures++;front_all=back_all=front_free_all=back_free_all=0;}else{if(k&&first_valid&&memcmp(first,t,R7_BLOCKS*16u)){reject(1,"ALLOCATOR_SNAPSHOT_CHANGED");failures++;}if(!k)first_valid=1;if(!old_free)front_free_all=0;if(!backend_free)back_free_all=0;if(!r7_allocation_absent(lifetime.front_state)){reject(2,"FRONT_ORIGINAL_ALLOCATION_IDENTITY_UNPROVEN");front_all=0;}if(!r7_allocation_absent(lifetime.backend_state)){reject(4,"BACKEND_ORIGINAL_ALLOCATION_IDENTITY_UNPROVEN");back_all=0;}}
 struct fr_snapshot fr;fr_take(&fr,fr_adapter,NULL);char name[64];snprintf(name,sizeof name,"front-readiness-%u.bin",k);if(!save(name,&fr,sizeof fr)){reject(128,"FRONT_DIAGNOSTIC_SAVE_FAILED");failures++;}unsigned need_init=0;if(!r7_front_retired(&fr,&need_init)){reject(8,"CS1_NOT_RETIRED");roots_all=0;}else if(need_init)render_initialization_required=1;
 if(!shadow_checkpoint(&fr,k)){reject(128,"SHADOW_OBSERVATION_SAVE_FAILED");roots_all=0;}
 static const uint32_t windows[][2]={{0x8100f7b0u,8},{0x817d7b48u,72},{0x817d7b90u,72},{FR_TASK+3388u,24},{FR_TASK+3428u,12},{0x8153ed18u,160},{0x8153e6d8u,160},{0x8153f358u,8},{0x8153f3a8u,8},{0x813f09b8u,4},{0x813f09c8u,4},{0x813f0bc8u,4},{0x813f0bd0u,4},{0x813f09c0u,4}};for(unsigned i=0;i<sizeof windows/sizeof *windows;i++)if(!rootwindow(windows[i][0],windows[i][1])){reject(16,"KNOWN_RTOS_PHYSICAL_ROOT_BUSY_OR_UNREADABLE");roots_all=0;}
 uint32_t slot[2];if(!ram(0x8100f7b0u,slot,8)||slot[0]!=0x800e454cu||slot[1]){reject(16,"CACHE_DISPATCH_NOT_ORIGINAL");roots_all=0;}if(!shellproof()){reject(32,"SHELL_OR_QUEUE_NOT_IDLE");roots_all=0;}snapshots++;if(k+1<3){struct timespec t={0,50000000};if(nanosleep(&t,NULL)){reject(128,"FRONT_SAMPLE_SLEEP_FAILED");failures++;}}}
 old_free=front_free_all;backend_free=back_free_all;front_allocation_retired=front_all;backend_allocation_retired=back_all;roots_clear=roots_all;int gate=backend_proof();
 nxl_live_progress("CHECK_FINAL","RUNNING","Rechecking the complete allocator before decision");
 allocator_final_revalidated=pool(next,3)&&first_valid&&!memcmp(first,next,R7_BLOCKS*16u);
 if(!allocator_final_revalidated){reject(1,"ALLOCATOR_CHANGED_DURING_DECISION");front_allocation_retired=backend_allocation_retired=0;}
 if(shadow_latched_references){struct fr_snapshot final_fr;fr_take(&final_fr,fr_adapter,NULL);
  if(!shadow_checkpoint(&final_fr,3)||!shadow_current_idle||shadow_check_failed||shadow_observations!=4){reject(16,"RETAINED_DESCRIPTOR_IDLE_OR_STABILITY_UNPROVEN");roots_clear=0;}
 }

 old_free=old_free&&front_free_all;backend_free=backend_free&&back_free_all;
 free(first);free(next);backend_retired=gate&&backend_allocation_retired&&allocator_final_revalidated;
 return !failures&&!category_failures&&snapshots==3&&front_allocation_retired&&backend_allocation_retired&&roots_clear&&backend_retired&&allocator_final_revalidated;}
static int result(int rc){char b[4096];int n=snprintf(b,sizeof b,"{\"format\":\"NX51_SESSION_REARM_R7\",\"version\":\"0.4.0-demo51-422\",\"exit\":%d,\"reason\":\"%s\",\"readonly\":1,\"native_calls\":0,\"CAP_allocations\":0,\"CAP_releases\":0,\"MMIO_reads\":%u,\"MMIO_writes\":0,\"ptrace_calls\":0,\"authenticated\":%u,\"metadata_verified\":%u,\"resource_snapshots\":%u,\"old_range_free\":%u,\"known_roots_clear\":%u,\"code_original\":%u,\"shell_parked\":%u,\"queue_empty\":%u,\"workers_idle\":%u,\"backend_range_free\":%u,\"backend_resources_retired\":%u,\"native_outcome_resolved\":%u,\"category_failure_mask\":%u,\"backend_samples\":%u,\"backend_metadata_verified\":%u,\"backend_pa\":%u,\"backend_va\":%u,\"old_pa\":%u,\"old_va\":%u,\"nonce\":%u,\"read_failures\":%u,\"read_faults\":%u,\"last_read_address\":%u,\"shell_reason\":\"%s\",\"shell_samples\":%u,\"shell_elapsed_ms\":%u,\"shell_syscall_number\":%llu,\"render_initialization_required\":%u,\"front_allocation_retired\":%u,\"backend_allocation_retired\":%u,\"allocator_layout_verified\":%u,\"allocator_final_revalidated\":%u,\"front_range_state\":\"%s\",\"backend_range_state\":\"%s\",\"background_activity_samples\":%u,\"latched_jpeg_reference_samples\":%u,\"latched_jpeg_reference_mask\":%u,\"retained_descriptor_references\":%u,\"retained_descriptor_observations\":%u,\"retained_descriptor_idle\":%u,\"retained_descriptor_stability_failed\":%u}\n",rc,reason,mmio_reads,authenticated,metadata_ok,snapshots,old_free,roots_clear,code_ok,parked,queue_empty,workers_idle,backend_free,backend_retired,native_resolved,category_failures,backend_samples,backend_metadata,backend_pa,backend_va,old_pa,old_va,old_nonce,read_failures,read_faults,last_address,shell_report.reason?shell_report.reason:"NOT_SAMPLED",shell_report.samples,shell_report.elapsed_ms,(unsigned long long)shell_report.frame.number,render_initialization_required,front_allocation_retired,backend_allocation_retired,lifetime.valid,allocator_final_revalidated,r7_range_name(lifetime.front_state),r7_range_name(lifetime.backend_state),preview_retirement_samples,latched_jpeg_reference_samples,latched_jpeg_reference_mask,shadow_latched_references,shadow_observations,shadow_current_idle,shadow_check_failed);return n>0&&n<(int)sizeof b&&save("result.json",b,(size_t)n);}
int main(int argc,char**argv){if(argc==2&&!strcmp(argv[1],"--version")){puts(VERSION);return 0;}if(argc!=9)return 2;for(int i=1;i<9;i+=2){if(!strcmp(argv[i],"--job"))job=argv[i+1];else if(!strcmp(argv[i],"--previous"))previous=argv[i+1];else if(!strcmp(argv[i],"--token"))token=argv[i+1];else if(!strcmp(argv[i],"--output"))output=argv[i+1];else return 2;}if(!jobpath(job)||!jobpath(previous)||!token||strlen(token)!=32||strspn(token,"0123456789abcdef")!=32||!safe(output)||strncmp(output,job,strlen(job))||output[strlen(job)]!='/'||strchr(output+strlen(job)+1,'/')||!no_links(job)||!no_links(previous))return 2;if(mkdir(output,0700)||!no_links(output))return 2;char p[1300];snprintf(p,sizeof p,"%s/reads.log",output);logf=fopen(p,"wx");int rc=1;
#define REQUIRE(c,r) do{if(!(c)){reason=(r);goto done;}}while(0)
 REQUIRE(logf,"REPORT_LOG_UNAVAILABLE");REQUIRE(decision_begin(),"RESET_RECORDER_OPEN_FAILED");nxl_live_progress("CHECK_AUTH","RUNNING","Checking the current task and supervisor");REQUIRE(auth(),"SUPERVISOR_LEASE_UNVERIFIED");authenticated=1;nxl_live_progress("CHECK_METADATA","RUNNING","Reading allocation and return records");REQUIRE(resource_metadata(),"RESOURCE_OPERATION_METADATA_UNPROVEN");metadata_ok=backend_metadata=1;if(old_bytes&&!r7_set_front_capacity(old_bytes)){reason="RESOURCE_CAPACITY_UNSUPPORTED";goto done;}if(!no_native_locks())reject(256,reason);REQUIRE(firmware(),"FIRMWARE_MISMATCH");REQUIRE(process_setup(),"RUNTIME_CODE_OR_WORKERS_UNPROVEN");if(!shell_check(1)){reject(32,shell_report.reason);goto done;}devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);REQUIRE(devfd>=0,"READONLY_DEVMEM_UNAVAILABLE");nxl_live_progress("CHECK_CODE","RUNNING","Verifying original code and idle context");REQUIRE(physical_guards(),"RTOS_CODE_NOT_ORIGINAL_OR_UNREADABLE");code_ok=1;if(!resources())goto done;native_resolved=1;reason="EMPTY_SESSION_RESOURCE_PROOF_VERIFIED";rc=0;
 done:if(!decision_finish(rc)){reject(128,"RESET_RECORDER_WRITE_FAILED");if(!rc)reason="RESET_RECORDER_WRITE_FAILED";rc=1;}fprintf(stderr,"NX51_REARM reason=%s readonly=1 native_calls=0 snapshots=%u front_free=%u backend_free=%u backend_resources_retired=%u native_outcome_resolved=%u\n",reason,snapshots,old_free,backend_free,backend_retired,native_resolved);if(!result(rc))rc=1;if(logf&&fclose(logf))rc=1;logf=NULL;if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);if(!rc){char b[4096],h[65];int n=snprintf(b,sizeof b,"NX51REARMPROOF3\njob=%s\nprevious=%s\ntoken=%s\nsource_record_sha256=%s\ntarget_record_sha256=%s\nresource_operations=%u\nresource_ranges=%u\nreadonly=1\nnative_calls=0\nresource_snapshots=3\nfront_allocation_retired=1\nbackend_allocation_retired=1\nallocator_layout_verified=1\nallocator_final_revalidated=1\nfront_range_state=%s\nbackend_range_state=%s\nbackend_resources_retired=1\nnative_outcome_resolved=1\nknown_roots_clear=1\ncode_original=1\nshell_parked=1\nqueue_empty=1\nworkers_idle=1\nreason=EMPTY_SESSION_RESOURCE_PROOF_VERIFIED\n",job,previous,token,resource_source_hash,resource_target_hash,inventory.operations,inventory.count,r7_range_name(lifetime.front_state),r7_range_name(lifetime.backend_state));if(n<=0||n>=(int)sizeof b)return 1;hexhash(b,(size_t)n,h);int z=snprintf(b+n,sizeof b-(size_t)n,"sha256=%s\nend=NX51REARMEND3\n",h);if(z<=0||z>=(int)sizeof b-n||!save("REARM.proof",b,(size_t)(n+z)))return 1;int f=open(output,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(f<0||fsync(f)){if(f>=0)close(f);return 1;}close(f);}return rc;
}
