/* SPDX-License-Identifier: AGPL-3.0-only. Durable orchestration only; no ISP APIs. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include "sha256.h"
#define VERSION "0.4.0-demo51-422"
#ifndef DEFAULT_ROOT
#define DEFAULT_ROOT "/mnt/mmc/NXDEMO51"
#endif
#ifndef OEM_SD_ALIAS
#define OEM_SD_ALIAS "/mnt/mmc"
#endif
#ifndef OEM_SD_TARGET
#define OEM_SD_TARGET "/opt/storage/sdcard"
#endif
#define SESSION_REVISION "resource-stage-r8a"
#define MAX_RECORD 8192
struct session {uint64_t seq,front_attempt,backend_attempt;unsigned verified,strength,requires_reset;char job[1024],front[1024],boot[64],state[32],resume[16],stage[16],reason[128],outcome[40],look[81];};
static const char *root;static char boot[64];static unsigned observed_boot_changed,boot_known;static volatile sig_atomic_t caught;
static int bounded(char *out,size_t cap,const char *a,const char *b){int n=snprintf(out,cap,"%s/%s",a,b);return n>0&&(size_t)n<cap;}
static int safe(const char *s,size_t max,int path){size_t n=strlen(s);if(!n||n>=max)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||(path&&(s[i]=='/'||s[i]=='.'))))return 0;return !path||(s[0]=='/'&&!strstr(s,"//")&&!strstr(s,"/../")&&!strstr(s,"/./")&&strcmp(s+n-1,"/")&&!(n>=3&&!strcmp(s+n-3,"/..")));}
static int realdir(const char *p){struct stat st;if(lstat(p,&st))return 0;if(!S_ISDIR(st.st_mode)){errno=ENOTDIR;return 0;}return 1;}
/* Accept only the exact OEM SD anchor. Descendant/state symlinks remain
 * refused. The logical path stays /mnt/mmc for existing proof contracts. */
static const char *sd_alias(void){
#ifdef HOST_MODEL
 const char *v=getenv("NX51_TEST_SD_ALIAS");if(v&&v[0]=='/')return v;
#endif
 return OEM_SD_ALIAS;
}
static const char *sd_target(void){
#ifdef HOST_MODEL
 const char *v=getenv("NX51_TEST_SD_TARGET");if(v&&v[0]=='/')return v;
#endif
 return OEM_SD_TARGET;
}
static int plain_directory_chain(const char *p){char b[2048];if(strlen(p)>=sizeof b){errno=ENAMETOOLONG;return 0;}strcpy(b,p);for(char *q=b+1;*q;q++)if(*q=='/'){*q=0;int ok=realdir(b);*q='/';if(!ok)return 0;}return realdir(b);}
static int allowed_directory(const char *p){
 struct stat st;if(lstat(p,&st))return 0;if(S_ISDIR(st.st_mode))return 1;
 if(!S_ISLNK(st.st_mode)||strcmp(p,sd_alias())){errno=ELOOP;return 0;}
 char target[2048];ssize_t n=readlink(p,target,sizeof target-1);if(n<0)return 0;target[n]=0;
 if(strcmp(target,sd_target())||!plain_directory_chain(sd_target())){errno=ELOOP;return 0;}return 1;
}
static int no_symlink_parents(const char *p){char b[2048];if(strlen(p)>=sizeof b){errno=ENAMETOOLONG;return 0;}strcpy(b,p);for(char *q=b+1;*q;q++)if(*q=='/'){*q=0;int ok=allowed_directory(b);*q='/';if(!ok)return 0;}return 1;}
/* Root-independent startup diagnostics; never change the operation result. */
static void diagnostic(const char *phase,const char *result,const char *path,int error){
 int saved=errno;char line[1600],log[2048],logdir[2048];int n=snprintf(line,sizeof line,"NX51SESSION revision=%s phase=%s result=%s errno=%d path=%s\n",SESSION_REVISION,phase,result,error,path?path:"-");
 if(n>0&&n<(int)sizeof line){fwrite(line,1,(size_t)n,stderr);fflush(stderr);
  if(bounded(logdir,sizeof logdir,sd_alias(),"NXDEMO51/logs")&&no_symlink_parents(logdir)&&realdir(logdir)&&bounded(log,sizeof log,logdir,"session.log")){
   int fd=open(log,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd>=0){struct stat st;if(!fstat(fd,&st)&&S_ISREG(st.st_mode)){ssize_t ignored=write(fd,line,(size_t)n);(void)ignored;fsync(fd);}close(fd);}
  }
 }errno=saved;
}
static int failure(const char *phase,const char *path,int rc){diagnostic(phase,"FAILED",path,errno);return rc;}
static int syncdir(const char *dir){int fd=open(dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;int ok=!fsync(fd);if(close(fd))ok=0;return ok;}
static int bytes(const char *p,char *out,size_t cap,size_t *n){int fd=open(p,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&(uint64_t)st.st_size<cap;size_t at=0;if(ok)for(;;){if(at>=cap-1){ok=0;break;}ssize_t r=read(fd,out+at,cap-1-at);if(r<0&&errno==EINTR)continue;if(r<0){ok=0;break;}if(!r)break;at+=(size_t)r;}if(close(fd))ok=0;if(!ok||!at)return 0;out[at]=0;*n=at;return strlen(out)==at;}
static int exact_text(const char *path,const char *text){char actual[MAX_RECORD];size_t n;return bytes(path,actual,sizeof actual,&n)&&n==strlen(text)&&!memcmp(actual,text,n);}
static int write_all(int fd,const char *text,size_t n){size_t at=0;while(at<n){ssize_t w=write(fd,text+at,n-at);if(w<0&&errno==EINTR)continue;if(w<=0)return 0;at+=(size_t)w;}return !fsync(fd);}
/* Mirrors/lease publication: verify actual contents, then same-inode fallback.
 * Durable state and current-job authority live in append-only ledgers below. */
static int atomic(const char *dir,const char *name,const char *text){
 if(!strcmp(name,"SESSION.state")||!strcmp(name,"SUMMARY.txt")||!strcmp(name,"current.job")){
  char path[2048];if(!bounded(path,sizeof path,dir,name))return 0;int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;
  int ok=write_all(fd,text,strlen(text));if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;return ok&&exact_text(path,text);
 }

 char dst[2048],tmp[2048];if(!bounded(dst,sizeof dst,dir,name)||snprintf(tmp,sizeof tmp,"%s/.%s.XXXXXX",dir,name)>=(int)sizeof tmp)return 0;
#ifdef HOST_MODEL
 if(getenv("NX51_TEST_FREEZE_MIRRORS")&&(!strcmp(name,"SESSION.state")||!strcmp(name,"SUMMARY.txt")||!strcmp(name,"current.job"))&&access(dst,F_OK)==0){errno=EIO;return 0;}
#endif
 int fd=mkostemp(tmp,O_CLOEXEC);if(fd<0)return 0;int ok=write_all(fd,text,strlen(text));if(close(fd))ok=0;
 if(ok&&rename(tmp,dst)==0&&syncdir(dir)&&exact_text(dst,text))return 1;
 diagnostic("MIRROR_REPLACE","READBACK_FAILED",dst,errno);
 fd=open(dst,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);ok=fd>=0;
 if(ok){struct stat st;ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&write_all(fd,text,strlen(text));if(close(fd))ok=0;}
 if(ok)ok=syncdir(dir)&&exact_text(dst,text);unlink(tmp);if(!ok)diagnostic("MIRROR_INPLACE","FAILED",dst,errno);return ok;
}
static int lockfile(const char *name,int nonblock){char p[2048];if(!bounded(p,sizeof p,root,name))return -1;int fd=open(p,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);struct stat st;if(fd<0)return -1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||flock(fd,LOCK_EX|(nonblock?LOCK_NB:0))){close(fd);return -1;}return fd;}
static void digest(const char *s,size_t n,char hex[65]){unsigned char d[32];digest_bytes(s,n,d);digest_hex(d,hex);}
static int unsigned_number(const char *s,uint64_t *v){if(!*s||(*s=='0'&&s[1]))return 0;uint64_t x=0;for(;*s;s++){if(*s<'0'||*s>'9'||x>UINT64_MAX/10)return 0;uint64_t y=x*10+(unsigned)(*s-'0');if(y<x)return 0;x=y;}*v=x;return 1;}
static int enum_value(const char *v,const char *const *values,unsigned n){for(unsigned i=0;i<n;i++)if(!strcmp(v,values[i]))return 1;return 0;}
static int valid(const struct session *s){
 static const char *const states[]={"NEED_STILL_INIT","FRONT_RUNNING","NEED_JPEG_INIT","BACKEND_RUNNING","COMPLETE","RETRYABLE","RESTART_REQUIRED"};
 static const char *const stages[]={"idle","front","backend"},*const resumes[]={"none","front","backend"};
 size_t n=strlen(root);if(!s->seq||!safe(s->job,sizeof s->job,1)||strncmp(s->job,root,n)||strncmp(s->job+n,"/JOB_",5)||strchr(s->job+n+1,'/')||!safe(s->boot,sizeof s->boot,0)||!safe(s->reason,sizeof s->reason,0)||!safe(s->outcome,sizeof s->outcome,0)||s->verified>1||s->requires_reset>1||!enum_value(s->state,states,7)||!enum_value(s->stage,stages,3)||!enum_value(s->resume,resumes,3))return 0;
 if(s->verified){if(!safe(s->front,sizeof s->front,1)||!no_symlink_parents(s->front)||!realdir(s->front))return 0;size_t a=strlen(s->front);if(a<7||strcmp(s->front+a-6,"/front"))return 0;
  if(strncmp(s->front,"/mnt/mmc/NXDEMO50/DEMO_",23)&&(strncmp(s->front,root,n)||s->front[n]!='/')){
#ifndef HOST_MODEL
   return 0;
#endif
  }
 }else if(strcmp(s->front,"-"))return 0;
 if(strcmp(s->look,"-")){if(!safe(s->look,sizeof s->look,0)||(s->strength!=25&&s->strength!=50&&s->strength!=75&&s->strength!=100))return 0;}else if(s->strength)return 0;
 if((!strcmp(s->state,"NEED_JPEG_INIT")||!strcmp(s->state,"BACKEND_RUNNING")||!strcmp(s->state,"COMPLETE"))&&!s->verified)return 0;
 if(!strcmp(s->state,"RESTART_REQUIRED")&&!s->requires_reset)return 0;
 if(strcmp(s->state,"RESTART_REQUIRED")&&strcmp(s->state,"COMPLETE")&&s->requires_reset)return 0;
 if(!strcmp(s->state,"COMPLETE")&&!s->requires_reset)return 0;
 if(!strcmp(s->state,"FRONT_RUNNING")&&strcmp(s->stage,"front"))return 0;
 if(!strcmp(s->state,"BACKEND_RUNNING")&&strcmp(s->stage,"backend"))return 0;
 return 1;
}
static int encode(const struct session *s,char out[MAX_RECORD]){char hash[65];int n=snprintf(out,MAX_RECORD,"NX51STATE1\nversion=" VERSION "\nseq=%llu\njob=%s\nstate=%s\nfront=%s\nfront_verified=%u\nrequires_reset=%u\nresume=%s\nstage=%s\nfront_attempt=%llu\nbackend_attempt=%llu\nboot=%s\noutcome=%s\nreason=%s\nlook_id=%s\nstrength=%u\n",(unsigned long long)s->seq,s->job,s->state,s->front,s->verified,s->requires_reset,s->resume,s->stage,(unsigned long long)s->front_attempt,(unsigned long long)s->backend_attempt,s->boot,s->outcome,s->reason,s->look,s->strength);if(n<=0||n>=MAX_RECORD-100)return 0;digest(out,(size_t)n,hash);return snprintf(out+n,MAX_RECORD-(size_t)n,"sha256=%s\nend=NX51END1\n",hash)<MAX_RECORD-n;}
static int field(char **next,const char *key,char *out,size_t cap){char *p=*next,*end=strchr(p,'\n');size_t n=strlen(key);if(!end||strncmp(p,key,n))return 0;size_t len=(size_t)(end-p)-n;if(len>=cap)return 0;memcpy(out,p+n,len);out[len]=0;*next=end+1;return 1;}
static int decode(char *text,size_t length,struct session *s){char original[MAX_RECORD];if(length>=sizeof original)return 0;memcpy(original,text,length+1);char *p=text,tmp[128];uint64_t u;memset(s,0,sizeof *s);
#define F(key,dest) do{if(!field(&p,key,dest,sizeof dest))return 0;}while(0)
#define U(key,dest) do{F(key,tmp);if(!unsigned_number(tmp,&u))return 0;dest=u;}while(0)
 F("",tmp);if(strcmp(tmp,"NX51STATE1"))return 0;F("version=",tmp);if(strcmp(tmp,VERSION))return 0;U("seq=",s->seq);F("job=",s->job);F("state=",s->state);F("front=",s->front);U("front_verified=",s->verified);U("requires_reset=",s->requires_reset);F("resume=",s->resume);F("stage=",s->stage);U("front_attempt=",s->front_attempt);U("backend_attempt=",s->backend_attempt);F("boot=",s->boot);F("outcome=",s->outcome);F("reason=",s->reason);F("look_id=",s->look);U("strength=",s->strength);
 F("sha256=",tmp);if(strlen(tmp)!=64)return 0;F("end=",tmp);if(strcmp(tmp,"NX51END1")||*p||!valid(s))return 0;char canonical[MAX_RECORD];return encode(s,canonical)&&!strcmp(canonical,original);
#undef F
#undef U
}
#include "journal.inc"
#include "rearm.inc"
static const char *next_action(const struct session *s){if(!strcmp(s->state,"NEED_STILL_INIT"))return "BOOT_INITIALIZE_STILL_AND_JPEG_THEN_START_PIPELINE";if(!strcmp(s->state,"NEED_JPEG_INIT"))return "OLD_SPLIT_JOB_CREATE_NEW_PIPELINE";if(!strcmp(s->state,"RETRYABLE")){if(strstr(s->reason,"_IDENTITY_FAILED")||strstr(s->reason,"_VERSION_"))return "INSTALL_MATCHING_PACKAGE_THEN_RETRY";return !strcmp(s->resume,"backend")?"REINITIALIZE_JPEG_THEN_RETRY":"INITIALIZE_STILL_THEN_RETRY";}if(!strcmp(s->state,"RESTART_REQUIRED"))return "CHECK_CURRENT_RESOURCES_THEN_INITIALIZE";if(!strcmp(s->state,"COMPLETE"))return "VIEW_JPEG_OR_NEW_SESSION";return "WAIT_FOR_TASK";}
static int summary(const struct session *s){char b[MAX_RECORD];snprintf(b,sizeof b,"NX51SUMMARY1\nversion=" VERSION "\njob_dir=%s\nstate=%s\nfront_dir=%s\nfront_verified=%u\nrequires_reset=%u\nlast_stage=%s\nlast_error=%s\noutcome=%s\nnext_action=%s\nfront_attempt=%llu\nbackend_attempt=%llu\nlook_id=%s\nstrength=%u\n",s->job,s->state,s->front,s->verified,s->requires_reset,s->stage,s->reason,s->outcome,next_action(s),(unsigned long long)s->front_attempt,(unsigned long long)s->backend_attempt,s->look,s->strength);return atomic(s->job,"SUMMARY.txt",b);}
static int commit(struct session *s,const char *event){
 char b[MAX_RECORD],p[2048],entry[512];if(!valid(s)||!encode(s,b)||!state_append(s,b))return 0;
 /* Human mirrors may be stale; a successful append is authoritative. */
 if(!atomic(s->job,"SESSION.state",b))diagnostic("STATE_MIRROR","STALE_JOURNAL_AUTHORITATIVE",s->job,errno);
 if(!summary(s))diagnostic("SUMMARY_MIRROR","STALE_JOURNAL_AUTHORITATIVE",s->job,errno);
 if(!bounded(p,sizeof p,s->job,"events.log"))return 1;
 int fd=open(p,O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){diagnostic("EVENT_APPEND","FAILED",p,errno);return 1;}
 int n=snprintf(entry,sizeof entry,"seq=%llu event=%s state=%s stage=%s outcome=%s reason=%s\n",(unsigned long long)s->seq,event,s->state,s->stage,s->outcome,s->reason);
 if(n<=0||n>=(int)sizeof entry||!write_all(fd,entry,(size_t)n))diagnostic("EVENT_APPEND","FAILED",p,errno);close(fd);return 1;
}
static int boot_value_known(const char *value){
#ifdef HOST_MODEL
 return safe(value,sizeof boot,0)&&strcmp(value,"UNKNOWN");
#else
 if(strlen(value)!=36)return 0;for(unsigned i=0;i<36;i++){if(i==8||i==13||i==18||i==23){if(value[i]!='-')return 0;}else if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')||(value[i]>='A'&&value[i]<='F')))return 0;}return 1;
#endif
}
static int proven_boot_change(const struct session *s){return boot_known&&boot_value_known(s->boot)&&strcmp(s->boot,boot)!=0;}
static int read_boot(void){
#ifdef HOST_MODEL
 const char *test=getenv("NX51_TEST_BOOT_ID");if(test){if(!safe(test,sizeof boot,0))return 0;strcpy(boot,test);boot_known=boot_value_known(boot);return 1;}
#endif
 char text[128];size_t n;int ok=bytes("/proc/sys/kernel/random/boot_id",text,sizeof text,&n);
 if(ok&&n>=2&&text[n-1]=='\n'){text[n-1]=0;if(boot_value_known(text)){strcpy(boot,text);boot_known=1;return 1;}errno=EINVAL;}
 else if(ok)errno=EINVAL;
 int error=errno;strcpy(boot,"UNKNOWN");boot_known=0;diagnostic("BOOT_OBSERVATION","UNAVAILABLE_NOT_REQUIRED","/proc/sys/kernel/random/boot_id",error);return 1;
}
static int token(char out[33]){unsigned char b[16];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);if(fd<0)return 0;int ok=read(fd,b,sizeof b)==sizeof b;close(fd);static const char h[]="0123456789abcdef";if(ok){for(unsigned i=0;i<16;i++){out[i*2]=h[b[i]>>4];out[i*2+1]=h[b[i]&15];}out[32]=0;}return ok;}
static uint64_t start_ticks(long pid){
#ifdef HOST_MODEL
 /* The sandbox omits /proc; model only process liveness. Production below
  * always binds to Linux start_ticks to detect PID reuse. */
 return kill((pid_t)pid,0)==0?1:0;
#else
 char p[80],b[2048];size_t n;snprintf(p,sizeof p,"/proc/%ld/stat",pid);if(!bytes(p,b,sizeof b,&n))return 0;char *q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;if(q[0]=='Z'||q[0]=='X')return 0;for(unsigned field=3;field<22;field++){q=strchr(q,' ');if(!q)return 0;q++;}char *end=strchr(q,' ');if(!end)return 0;*end=0;uint64_t v;return unsigned_number(q,&v)?v:0;
#endif
}
#include "../../common/session_lease_paths.h"
static const char*lease_directory(void){
#ifdef HOST_MODEL
 return root;
#else
 return NX51_LEASE_DIRECTORY;
#endif
}
static const char*lease_name(void){
#ifdef HOST_MODEL
 return "active.lease";
#else
 return NX51_LEASE_NAME;
#endif
}
static int lease_live(const char *required,int require_full){char p[2048],b[512],pidstr[32],ticksstr[32],t[40],mode[16],end[32];size_t n;if(!bounded(p,sizeof p,lease_directory(),lease_name())||!bytes(p,b,sizeof b,&n))return 0;char *q=b;int full=0;if(!field(&q,"pid=",pidstr,sizeof pidstr)||!field(&q,"start_ticks=",ticksstr,sizeof ticksstr)||!field(&q,"token=",t,sizeof t))return 0;
 if(!strncmp(q,"mode=",5)){if(!field(&q,"mode=",mode,sizeof mode)||(strcmp(mode,"normal")&&strcmp(mode,"full")&&strcmp(mode,"rearm")))return 0;full=!strcmp(mode,"full")?1:!strcmp(mode,"rearm")?2:0;if(!field(&q,"end=",end,sizeof end)||strcmp(end,"NX51LEASE2"))return 0;}
 else if(!field(&q,"end=",end,sizeof end)||strcmp(end,"NX51LEASE1"))return 0;
 if(*q||strlen(t)!=32)return 0;uint64_t pid,ticks;if(!unsigned_number(pidstr,&pid)||pid<2||pid>2147483647u||!unsigned_number(ticksstr,&ticks)||!ticks||start_ticks((long)pid)!=ticks)return 0;return (!required||!strcmp(required,t))&&(!require_full||full==require_full);}
static int authorize(void){const char *t=getenv("NX51_SESSION_TOKEN");return t&&strlen(t)==32&&lease_live(t,0);}
static int authorize_full(void){const char *t=getenv("NX51_SESSION_TOKEN");return t&&strlen(t)==32&&lease_live(t,1);}
static int authorize_rearm(void){const char *t=getenv("NX51_SESSION_TOKEN");return t&&strlen(t)==32&&lease_live(t,2);}
static int eligible_full(const struct session *s){return !strcmp(s->state,"RESTART_REQUIRED")&&(!strcmp(s->reason,"LEGACY_STATE_DIVERGENCE")||!strcmp(s->reason,"FRONT_RESTART_OR_PATH_INVALID"))&&!s->backend_attempt&&strcmp(s->stage,"backend");}
static int new_session(struct session *s,const struct session *previous){char unique[33],pointer[2048],hash[65];struct timespec now;if(clock_gettime(CLOCK_REALTIME,&now)){diagnostic("JOB_CLOCK","FAILED","CLOCK_REALTIME",errno);return 0;}if(!token(unique)){diagnostic("JOB_TOKEN","FAILED","/dev/urandom",errno);return 0;}memset(s,0,sizeof *s);s->seq=1;strcpy(s->boot,boot);strcpy(s->state,"NEED_STILL_INIT");strcpy(s->front,"-");strcpy(s->resume,"front");strcpy(s->stage,"idle");strcpy(s->reason,"NOT_STARTED");strcpy(s->outcome,"NONE");strcpy(s->look,"-");if(snprintf(s->job,sizeof s->job,"%s/JOB_%lld_%ld_%.8s",root,(long long)now.tv_sec,(long)getpid(),unique)>=(int)sizeof s->job||mkdir(s->job,0700)){diagnostic("JOB_MKDIR","FAILED",s->job,errno);return 0;}diagnostic("JOB_MKDIR","CREATED",s->job,0);if(previous&&previous->requires_reset){s->requires_reset=1;strcpy(s->state,"RESTART_REQUIRED");strcpy(s->reason,"REAL_KERNEL_RESTART_NOT_VERIFIED");strcpy(s->outcome,"SAFE_RELEASED_COLD");}
 if(!commit(s,"NEW_SESSION")){diagnostic("JOB_INITIAL_COMMIT","FAILED",s->job,errno);return 0;}int n=snprintf(pointer,sizeof pointer,"NX51CURRENT1\njob=%s\n",s->job);digest(pointer,(size_t)n,hash);snprintf(pointer+n,sizeof pointer-(size_t)n,"sha256=%s\nend=NX51END1\n",hash);if(!pointer_append(s->job)){diagnostic("CURRENT_LEDGER","FAILED",root,errno);return 0;}if(!atomic(root,"current.job",pointer))diagnostic("CURRENT_MIRROR","STALE_LEDGER_AUTHORITATIVE",root,errno);return 1;}
static int running(const struct session *s){return !strcmp(s->state,"FRONT_RUNNING")||!strcmp(s->state,"BACKEND_RUNNING");}
static int reconcile(struct session*s,int write_back){
 /* boot_id is diagnostic context, never proof that the RTOS/CAP retired. */
 if(running(s)&&!lease_live(NULL,0)){strcpy(s->state,"RESTART_REQUIRED");strcpy(s->outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s->reason,"SUPERVISOR_OR_WORKER_INTERRUPTED");s->requires_reset=1;s->seq++;return !write_back||commit(s,"INTERRUPTED_RECONCILE");}
 return 1;
}
static int print_status(const struct session *s,int exists){printf("NX51STATUS1\nversion=" VERSION "\njob=%s\njob_dir=%s\nstate=%s\nfront=%s\nfront_dir=%s\nfront_verified=%u\nrequires_reset=%u\nresume=%s\nstage=%s\noutcome=%s\nreason=%s\nlast_error=%s\nnext_action=%s\nfront_attempt=%llu\nbackend_attempt=%llu\nlook_id=%s\nstrength=%u\nboot=%s\ncurrent_boot=%s\nkernel_boot_changed=%u\nend=NX51END1\n",exists?s->job:"-",exists?s->job:"-",exists?s->state:"NEED_STILL_INIT",exists?s->front:"-",exists?s->front:"-",exists?s->verified:0,exists?s->requires_reset:0,exists?s->resume:"front",exists?s->stage:"idle",exists?s->outcome:"NONE",exists?s->reason:"NOT_STARTED",exists?s->reason:"NOT_STARTED",exists?next_action(s):"BOOT_INITIALIZE_STILL_AND_JPEG_THEN_START_PIPELINE",(unsigned long long)(exists?s->front_attempt:0),(unsigned long long)(exists?s->backend_attempt:0),exists?s->look:"-",exists?s->strength:0,exists?s->boot:boot,boot,(observed_boot_changed||(exists&&proven_boot_change(s))));return ferror(stdout)?1:0;}
/* Status never creates a lock or state file. An existing shared lock keeps
 * append readers away from an incomplete record while its writer owns EX. */
static int readonly_state_lock(void){
 char path[2048];if(!bounded(path,sizeof path,root,"state.lock"))return -1;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return errno==ENOENT?-2:-1;
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)){int e=errno?errno:EINVAL;close(fd);errno=e;return -1;}
 int rc;do{rc=flock(fd,LOCK_SH);}while(rc&&errno==EINTR);if(rc){int e=errno;close(fd);errno=e;return -1;}return fd;
}
static int readonly_status(void){
 struct session s;if(!realdir(root)){if(errno==ENOENT)return print_status(&s,0);return failure("STATUS_ROOT",root,1);}
 int fd=readonly_state_lock();if(fd==-1)return failure("STATUS_SHARED_LOCK",root,1);
 if(fd==-2){char path[2048];struct stat st;const char *names[2]={"CURRENT.journal","current.job"};for(unsigned i=0;i<2;i++)if(bounded(path,sizeof path,root,names[i])&&(!lstat(path,&st)||errno!=ENOENT)){errno=ENOENT;return failure("STATUS_LOCK_MISSING",root,1);}return print_status(&s,0);}
 int found=current(&s),rc;if(found<0){fputs("INVALID_SESSION_RECORD\n",stderr);rc=2;}else{if(found)observed_boot_changed=proven_boot_change(&s);rc=found&&!reconcile(&s,0)?2:print_status(&s,found);}close(fd);return rc;
}
#include "install_preflight.inc"
#include "resource_grant.inc"
static void on_signal(int sig){caught=sig;}
#include "progress_publish.inc"
static int supervise(int argc,char **argv,int prepare_full){int runfd=lockfile("run.lock",1);if(runfd<0){fputs("SESSION_BUSY\n",stderr);return failure("RUN_LOCK",root,75);}int statefd=lockfile("state.lock",0);if(statefd<0){int e=errno;close(runfd);errno=e;return failure("STATE_LOCK",root,1);}struct session s,previous={0};int exists=current(&s),ok=exists>=0;if(ok&&!exists)ok=prepare_full?0:new_session(&s,NULL);else if(ok&&!prepare_full){if(running(&s)){strcpy(s.state,"RESTART_REQUIRED");strcpy(s.outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s.reason,"ORPHANED_RUNNING_STATE");s.requires_reset=1;s.seq++;ok=commit(&s,"RUN_LOCK_ACQUIRED_OVER_INTERRUPTED_STATE");}else ok=reconcile(&s,1);}if(ok&&prepare_full==2&&running(&s)){strcpy(s.state,"RESTART_REQUIRED");s.requires_reset=1;strcpy(s.outcome,"UNKNOWN_REQUIRE_RESTART");strcpy(s.reason,"INTERRUPTED_TASK_RESOURCE_CHECK");s.seq++;ok=commit(&s,"RESOURCE_CHECK_ORPHANED_TASK");}
 close(statefd);if(!ok){int e=errno;close(runfd);errno=e;return failure("SESSION_LOAD_OR_CREATE",root,1);}if((prepare_full==1&&!eligible_full(&s))||(prepare_full==2&&!rearm_previous(&s,&previous))){print_status(&s,1);close(runfd);return failure(prepare_full==2?"REARM_EMPTY_SESSION_ELIGIBILITY":"PREPARE_FULL_ELIGIBILITY",s.job,76);}
 if(!prepare_full&&(!strcmp(s.state,"COMPLETE")||!strcmp(s.state,"RESTART_REQUIRED"))){int rc=print_status(&s,1);close(runfd);return !strcmp(s.state,"RESTART_REQUIRED")?76:rc;}
 char t[33],lease[256];uint64_t ticks=start_ticks(getpid());if(!ticks){int e=errno;close(runfd);errno=e;return failure("PROCESS_START_TICKS","/proc/self/stat",1);}if(!token(t)){int e=errno;close(runfd);errno=e;return failure("LEASE_TOKEN","/dev/urandom",1);}snprintf(lease,sizeof lease,"pid=%ld\nstart_ticks=%llu\ntoken=%s\nmode=%s\nend=NX51LEASE2\n",(long)getpid(),(unsigned long long)ticks,t,prepare_full==2?"rearm":prepare_full?"full":"normal");if(!realdir(lease_directory())||!no_symlink_parents(lease_directory())||!atomic(lease_directory(),lease_name(),lease)){int e=errno;close(runfd);errno=e;return failure("LEASE_COMMIT",root,1);}
 if(!safe(argv[0],2048,1)||!no_symlink_parents(argv[0])){close(runfd);return 1;}struct stat st;if(lstat(argv[0],&st)||!S_ISREG(st.st_mode)){close(runfd);return 1;}
 pid_t child=fork();if(child<0){close(runfd);return 1;}if(!child){close(runfd);setenv("NX51_SESSION_ROOT",root,1);setenv("NX51_SESSION_TOKEN",t,1);setenv("NX51_JOB",s.job,1);if(prepare_full==1)setenv("NX51_PREPARE_FULL_ONLY","1",1);else unsetenv("NX51_PREPARE_FULL_ONLY");if(prepare_full==2){setenv("NX51_REARM_ONLY","1",1);setenv("NX51_PREVIOUS_JOB",previous.job,1);}else{unsetenv("NX51_REARM_ONLY");unsetenv("NX51_PREVIOUS_JOB");}char **args=calloc((size_t)argc+2,sizeof(char*));if(!args)_exit(127);args[0]="/bin/sh";for(int i=0;i<argc;i++)args[i+1]=argv[i];execv("/bin/sh",args);_exit(127);}
 struct sigaction action={0};action.sa_handler=on_signal;sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);sigaction(SIGHUP,&action,NULL);int status=0,sent=0;pid_t got;
 do{got=waitpid(child,&status,0);if(got<0&&errno==EINTR&&caught&&!sent){kill(child,caught);sent=1;}}while(got<0&&errno==EINTR);int rc=got==child&&WIFEXITED(status)?WEXITSTATUS(status):128+(caught?caught:SIGTERM);
 statefd=lockfile("state.lock",0);if(statefd>=0){int loaded=current(&s);const char *prepared=prepare_full==2?"EMPTY_SESSION_RESOURCE_PROOF_VERIFIED":"FULL_TEST_PREPARED_NO_REUSE";if(prepare_full&&!rc&&(loaded!=1||strcmp(s.state,"NEED_STILL_INIT")||strcmp(s.reason,prepared)||s.requires_reset))rc=76;if(loaded==1&&(running(&s)||(prepare_full==1&&rc&&!s.requires_reset&&!strcmp(s.reason,prepared)))){strcpy(s.state,"RESTART_REQUIRED");strcpy(s.reason,prepare_full==2?"REARM_WORKER_FAILED":prepare_full?"FULL_PREPARE_WORKER_FAILED":"WORKER_EXIT_WITHOUT_TERMINAL_COMMIT");strcpy(s.outcome,"UNKNOWN_REQUIRE_RESTART");s.requires_reset=1;s.seq++;if(!commit(&s,"SUPERVISOR_TERMINALIZED"))rc=1;else if(!rc)rc=76;}char p[2048];if(bounded(p,sizeof p,lease_directory(),lease_name()))unlink(p);syncdir(root);close(statefd);}else rc=1;close(runfd);return rc;}
int main(int argc,char **argv){if(argc>1&&!strcmp(argv[1],"--publish-progress"))return publish_progress(argc,argv);if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_SESSION_VERSION=" VERSION "; DURABLE_JOB_STATE1; SESSION_REVISION=" SESSION_REVISION "; OEM_SD_ALIAS_BOOT_OPTIONAL_R2; STATE_JOURNAL_R4; FRESH_SRW_EVERY_JOB_R4; EMPTY_SESSION_RESOURCE_REARM_R7; RESOURCE_REARM_R7; PREFLIGHT_HOLD_RECOVERY_R7E; INSTALL_PREFLIGHT_R7F; RESET_ROUND_LOCKS_R7F; ALLOCATION_LIFETIME_PROOF2_R7K; RESOURCE_FACTS_PROOF3_R7N; BOOT_ID_OBSERVATION_ONLY_R7N");return 0;}int at=1;if(argc>2&&argv[1][0]=='/'){root=argv[1];at=2;}else root=DEFAULT_ROOT;diagnostic("ENTRY","START",root,0);if(at>=argc||!safe(root,1024,1)){errno=EINVAL;return failure("ARGUMENTS",root,2);}if(!read_boot())return failure("BOOT_OBSERVATION",root,2);
#ifndef HOST_MODEL
 if(strcmp(root,DEFAULT_ROOT)){errno=EINVAL;return failure("FIXED_ROOT",root,2);}
#endif
 const char *cmd=argv[at++];if(!strcmp(cmd,"status"))return readonly_status();
 if(!strcmp(cmd,"check-install-preflight")){if(at+2!=argc)return 64;return readonly_install_preflight(argv[at],argv[at+1]);}
 if(!realdir(root)){if(errno!=ENOENT)return failure("ROOT_LSTAT",root,1);if(!no_symlink_parents(root))return failure("ROOT_PARENTS",root,1);if(mkdir(root,0700))return failure("ROOT_MKDIR",root,1);diagnostic("ROOT_MKDIR","CREATED",root,0);}if(!strcmp(cmd,"run")){if(at>=argc)return 2;return supervise(argc-at,argv+at,0);}
 if(!strcmp(cmd,"prepare-full")){
  if(at+2!=argc||strcmp(argv[at+1],"--prepare-full-worker")){errno=EINVAL;return failure("PREPARE_FULL_ARGUMENTS",root,2);}
#ifndef HOST_MODEL
  if(strcmp(argv[at],"/opt/usr/nx-lut-lite/memory-test.sh")){errno=EPERM;return failure("PREPARE_FULL_SCRIPT",argv[at],77);}
#endif
  return supervise(argc-at,argv+at,1);
 }
 if(!strcmp(cmd,"rearm")){
  if(at+2!=argc||strcmp(argv[at+1],"--rearm-worker")){errno=EINVAL;return failure("REARM_ARGUMENTS",root,2);}
#ifndef HOST_MODEL
  if(strcmp(argv[at],"/opt/usr/nx-lut-lite/rearm-session.sh")){errno=EPERM;return failure("REARM_SCRIPT",argv[at],77);}
#endif
  return supervise(argc-at,argv+at,2);
 }
 if(authorize_rearm()&&strcmp(cmd,"rearm-fresh")&&strcmp(cmd,"verify-preflight-hold")&&strcmp(cmd,"verify-resource-proof")&&strcmp(cmd,"renew-front-grant")&&strcmp(cmd,"fail")){errno=EPERM;return failure("REARM_MUTATOR",cmd,77);}
 if((!strcmp(cmd,"verify-preflight-hold")||!strcmp(cmd,"verify-resource-proof")||!strcmp(cmd,"renew-front-grant"))&&!authorize_rearm()){errno=EPERM;return failure("PREFLIGHT_HOLD_AUTH",cmd,77);}
 if(!strcmp(cmd,"rearm-fresh")&&!authorize_rearm()){errno=EPERM;return failure("REARM_AUTH",cmd,77);}
 if(authorize_full()&&strcmp(cmd,"fresh-front")&&strcmp(cmd,"fail")){errno=EPERM;return failure("PREPARE_FULL_MUTATOR",cmd,77);}
 if(!strcmp(cmd,"fresh-front")&&!authorize_full()){errno=EPERM;return failure("PREPARE_FULL_AUTH",cmd,77);}int outside_new=!strcmp(cmd,"new")&&!authorize();int runfd=-1;if(outside_new){runfd=lockfile("run.lock",1);if(runfd<0)return 75;}else if(!authorize())return 77;int fd=lockfile("state.lock",0);if(fd<0){if(runfd>=0)close(runfd);return 1;}struct session s;int found=current(&s),rc=1;if(found<0)goto done;if(!strcmp(cmd,"new")){if(found&&!reconcile(&s,1))goto done;if(found&&(running(&s)||!strcmp(s.state,"RESTART_REQUIRED")))goto done;struct session fresh;if(new_session(&fresh,found?&s:NULL))rc=print_status(&fresh,1);goto done;}if(found!=1||(!authorize_full()&&!authorize_rearm()&&!reconcile(&s,1)))goto done;
 if(!strcmp(cmd,"verify-preflight-hold")){
  struct session previous;if(at==argc&&preflight_legacy(&s)&&rearm_previous(&s,&previous))rc=0;
  goto done;
 }
 if(!strcmp(cmd,"renew-front-grant")){
  struct session source;if(at+1==argc&&rearm_previous(&s,&source)&&renew_resource_grant(&s,&source,argv[at]))rc=0;
  goto done;
 }
 if(!strcmp(cmd,"verify-resource-proof")){
  struct session source;if(at+1==argc&&rearm_previous(&s,&source)&&rearm_receipt(&s,&source,argv[at]))rc=0;
  goto done;
 }
 if(!strcmp(cmd,"rearm-fresh")){
  struct session previous;if(at+1!=argc||!rearm_previous(&s,&previous)||!rearm_receipt(&s,&previous,argv[at])||!lock_reset_receipt(&s,argv[at])||!resource_grant_receipt(&s,argv[at])){errno=EPERM;diagnostic("REARM_PROOF","UNVERIFIED_HELD",s.job,errno);goto done;}
  if(s.front_attempt||s.backend_attempt||s.verified){struct session fresh;if(!new_session(&fresh,NULL))goto done;s=fresh;}
  s.verified=0;strcpy(s.front,"-");s.requires_reset=0;strcpy(s.state,"NEED_STILL_INIT");strcpy(s.resume,"front");strcpy(s.stage,"idle");strcpy(s.reason,"EMPTY_SESSION_RESOURCE_PROOF_VERIFIED");strcpy(s.outcome,"VERIFIED_RESOURCE_REARM");strcpy(s.boot,boot);
 }
 else if(!strcmp(cmd,"fresh-front")){
  if(at+1!=argc||!eligible_full(&s)||!safe(argv[at],sizeof s.front,1)||!realdir(argv[at])||!no_symlink_parents(argv[at]))goto done;
  size_t joblen=strlen(s.job);if(strlen(argv[at])<=joblen)goto done;const char *f=argv[at]+joblen;if(strncmp(argv[at],s.job,joblen)||strncmp(f,"/front_",7))goto done;f+=7;const char *slash=strchr(f,'/');if(!slash||strcmp(slash,"/front"))goto done;char number[32];size_t len=(size_t)(slash-f);uint64_t attempt;if(!len||len>=sizeof number)goto done;memcpy(number,f,len);number[len]=0;if(!unsigned_number(number,&attempt)||!attempt||attempt>UINT32_MAX)goto done;
  if(s.front_attempt<attempt)s.front_attempt=attempt;if(!s.front_attempt)s.front_attempt=1;s.verified=0;s.requires_reset=0;strcpy(s.front,"-");strcpy(s.state,"NEED_STILL_INIT");strcpy(s.resume,"front");strcpy(s.stage,"idle");strcpy(s.reason,"FULL_TEST_PREPARED_NO_REUSE");strcpy(s.outcome,"VERIFIED_FALSE_HOLD_RESET");strcpy(s.look,"raw422");s.strength=100;strcpy(s.boot,boot);
 }
 else if(!strcmp(cmd,"import-front")){errno=EPERM;diagnostic("CACHE_IMPORT","DISABLED_FRESH_ONLY",root,errno);goto done;}
 else if(!strcmp(cmd,"configure")){if(at+2!=argc||strcmp(argv[at],"raw422")||strcmp(argv[at+1],"100"))goto done;uint64_t u;if(!unsigned_number(argv[at+1],&u)||(u!=25&&u!=50&&u!=75&&u!=100))goto done;if(strcmp(s.look,"-")&&(strcmp(s.look,argv[at])||s.strength!=u))goto done;strcpy(s.look,argv[at]);s.strength=(unsigned)u;}
 else if(!strcmp(cmd,"begin")){if(at+1!=argc||!strcmp(s.look,"-"))goto done;const char *stage=argv[at];if(!strcmp(stage,"pipeline")){
 if(s.requires_reset||s.verified||(strcmp(s.state,"NEED_STILL_INIT")&&(strcmp(s.state,"RETRYABLE")||strcmp(s.resume,"front"))))goto done;
 strcpy(s.state,"FRONT_RUNNING");s.front_attempt++;s.backend_attempt++;stage="front";
 }else if(!strcmp(stage,"front")){if(strcmp(s.state,"NEED_STILL_INIT")&&(strcmp(s.state,"RETRYABLE")||strcmp(s.resume,"front")))goto done;strcpy(s.state,"FRONT_RUNNING");s.front_attempt++;}else if(!strcmp(stage,"backend")){if(s.requires_reset||!s.verified||(strcmp(s.state,"NEED_JPEG_INIT")&&(strcmp(s.state,"RETRYABLE")||strcmp(s.resume,"backend"))))goto done;strcpy(s.state,"BACKEND_RUNNING");s.requires_reset=0;s.backend_attempt++;}else goto done;strcpy(s.stage,stage);strcpy(s.resume,stage);strcpy(s.boot,boot);strcpy(s.outcome,"RUNNING");strcpy(s.reason,!strcmp(argv[at],"pipeline")?"PIPELINE_STARTED":"TASK_STARTED");}
 else if(!strcmp(cmd,"pipeline-ok")){
  char expected[1100];if(at+1!=argc||strcmp(s.state,"FRONT_RUNNING")||strcmp(s.reason,"PIPELINE_STARTED")||s.verified||!s.front_attempt||!s.backend_attempt||
   snprintf(expected,sizeof expected,"%s/front_%llu/front",s.job,(unsigned long long)s.front_attempt)>=(int)sizeof expected||strcmp(argv[at],expected)||!realdir(expected)||!no_symlink_parents(expected))goto done;
  strcpy(s.front,expected);s.verified=1;strcpy(s.state,"COMPLETE");strcpy(s.resume,"none");strcpy(s.stage,"backend");strcpy(s.outcome,"VERIFIED_COMPLETE");strcpy(s.reason,"SRW_LUT_JPEG_SAVED");s.requires_reset=1;
 }
 else if(!strcmp(cmd,"front-ok")){if(at+1!=argc||!safe(argv[at],sizeof s.front,1)||!realdir(argv[at])||!no_symlink_parents(argv[at]))goto done;if(!strcmp(cmd,"front-ok")){if(strcmp(s.state,"FRONT_RUNNING"))goto done;}else if(strcmp(s.state,"NEED_STILL_INIT")&&(strcmp(s.state,"RETRYABLE")||strcmp(s.resume,"front")))goto done;strcpy(s.front,argv[at]);s.verified=1;strcpy(s.state,"NEED_JPEG_INIT");strcpy(s.resume,"backend");strcpy(s.reason,"FRONT_VERIFIED_PREPARE_JPEG");strcpy(s.outcome,"VERIFIED_FRONT");}
 else if(!strcmp(cmd,"backend-ok")){if(at!=argc||strcmp(s.state,"BACKEND_RUNNING"))goto done;strcpy(s.state,"COMPLETE");strcpy(s.resume,"none");strcpy(s.outcome,"VERIFIED_COMPLETE");strcpy(s.reason,"JPEG_SAVED");s.requires_reset=1;}
 else if(!strcmp(cmd,"fail")){if(at+2!=argc||!safe(argv[at+1],sizeof s.reason,0))goto done;const char *outcome=argv[at];if(!strcmp(outcome,"UNKNOWN_REQUIRE_RESTART")||!strcmp(outcome,"SAFE_RELEASED_COLD")||!strcmp(outcome,"SAFE_RETAINED_COLD")){strcpy(s.state,"RESTART_REQUIRED");s.requires_reset=1;}else if(!strcmp(outcome,"SAFE_NO_SUBMIT")||!strcmp(outcome,"SAFE_RELEASED")){if(!strcmp(s.state,"COMPLETE")||!strcmp(s.state,"RESTART_REQUIRED")||s.requires_reset)goto done;strcpy(s.state,"RETRYABLE");s.requires_reset=0;if(!strcmp(s.resume,"none"))strcpy(s.resume,s.verified?"backend":"front");}else goto done;strcpy(s.reason,argv[at+1]);strcpy(s.outcome,outcome);strcpy(s.boot,boot);}
 else goto done;s.seq++;if(commit(&s,cmd))rc=0;
done:close(fd);if(runfd>=0)close(runfd);return rc;}
