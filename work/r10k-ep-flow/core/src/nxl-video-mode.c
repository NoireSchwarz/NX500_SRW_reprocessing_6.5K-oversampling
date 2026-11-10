/* SPDX-License-Identifier: AGPL-3.0-only. Explicit reversible process-RAM
 * resolution mapping. No on-disk app patch, native call or camera flash.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include "sha256.h"
#include "../../common/video_mode_patch.h"
#define APP "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
static const uint32_t context_words[24]={0xea00000au,0xe3008438u,0xe3a0ad1eu,0xea000007u,0xe3a08e2du,0xe3a0ac05u,0xea000004u,0xe3a08e1eu,0xe3a0ad0au,0xea000001u,0xe3a08e87u,0xe3a0ac0fu,0xe1a00004u,0xebfffe00u,0xe1a06000u,0xe1a00004u,0xebfffe6eu,0xe1a07000u,0xe300010au,0xeb046d4du,0xe3a05000u,0xe58d5128u,0xe58d512cu,0xe1a01004u};
static volatile sig_atomic_t cancelled;
struct thread {int tid,attached,stopped,signal;};
struct camera {int pid,mem;uint64_t start;uint32_t movie_base,user_base;unsigned count;struct thread threads[192];};
static uint64_t ms(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return(uint64_t)t.tv_sec*1000u+t.tv_nsec/1000000u;}
static void stopped(int s){(void)s;cancelled=1;}
static int text(const char*p,char*b,unsigned cap){int f=open(p,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;ssize_t n=read(f,b,cap-1);int ok=n>=0&&n<(ssize_t)cap-1;if(n>=0)b[n]=0;if(close(f))ok=0;return ok;}
static uint64_t identity(int pid){char p[100],b[2048];snprintf(p,sizeof p,"/proc/%d/stat",pid);if(!text(p,b,sizeof b))return 0;char*q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;for(unsigned i=3;i<22;i++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}return strtoull(q,NULL,10);}
static int camera_pid(void){DIR*d=opendir("/proc");if(!d)return 0;int pid=0;struct dirent*e;while((e=readdir(d))){char*end;long p=strtol(e->d_name,&end,10);if(*end||p<=1||p>2147483647)continue;char f[100],target[256];snprintf(f,sizeof f,"/proc/%ld/exe",p);ssize_t n=readlink(f,target,sizeof target-1);if(n<0||n>=(ssize_t)sizeof target-1)continue;target[n]=0;if(!strcmp(target,APP)){if(pid){pid=0;break;}pid=(int)p;}}closedir(d);return pid;}
static int firmware(void){char b[100],ver[20],model[20];if(!text("/etc/version.info",b,sizeof b)||sscanf(b,"%19[^\r\n]%*[\r\n]%19[^\r\n]",ver,model)!=2||strcmp(ver,"1.12")||strcmp(model,"NX500"))return 0;
 FILE*f=fopen(APP,"rb");if(!f)return 0;struct sha256 h;sha256_init(&h);unsigned char buf[65536],digest[32];size_t n;uint64_t bytes=0;while((n=fread(buf,1,sizeof buf,f))){sha256_update(&h,buf,n);bytes+=n;}int ok=!ferror(f);if(fclose(f))ok=0;char hash[65];sha256_final(&h,digest);digest_hex(digest,hash);return ok&&bytes==4773516u&&!strcmp(hash,"55dcc78143724435a2e38b9e0d809960bd728668634bbcf7532ce137f882eb04");}
static int read_word(void*v,uint32_t at,uint32_t*out){struct camera*c=v;if(at!=VM_HEIGHT&&at!=VM_WIDTH&&(!c->movie_base||(at!=c->movie_base+0xa04u&&at!=c->movie_base+0xa08u)))return 0;return pread(c->mem,out,4,(off_t)at)==4;}
static int vp_rate_context(struct camera*c);
static int code_context(struct camera*c){uint32_t b[24];if(pread(c->mem,b,sizeof b,0xe4400)!=sizeof b)return 0;for(unsigned i=0;i<24;i++)if(i!=10&&i!=11&&b[i]!=context_words[i])return 0;return vp_rate_context(c);}
static int busy(struct camera*c){
 char path[100];snprintf(path,sizeof path,"/proc/%d/fd",c->pid);DIR*d=opendir(path);if(!d)return -1;struct dirent*e;int result=0;
 while((e=readdir(d))){char*end;long fd=strtol(e->d_name,&end,10);if(*end||fd<0)continue;char target[512],info[4096];snprintf(path,sizeof path,"/proc/%d/fd/%ld",c->pid,fd);ssize_t n=readlink(path,target,sizeof target-1);if(n<0){if(errno==ENOENT)continue;result=-1;break;}if(n>=(ssize_t)sizeof target-1){result=-1;break;}target[n]=0;
  const char*ext=strrchr(target,'.');if(!ext||(strcasecmp(ext,".mp4")&&strcasecmp(ext,".srw")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
  snprintf(path,sizeof path,"/proc/%d/fdinfo/%ld",c->pid,fd);if(!text(path,info,sizeof info)){result=-1;break;}char*q=strstr(info,"flags:");unsigned flags;if(!q||sscanf(q+6,"%o",&flags)!=1){result=-1;break;}if((flags&O_ACCMODE)!=O_RDONLY){result=1;break;}
 }closedir(d);return result;
}
static int ready(void*v){struct camera*c=v;struct stat st;return !cancelled&&identity(c->pid)==c->start&&code_context(c)&&busy(c)==0&&lstat("/tmp/nx-lut-lite/engine.lock",&st)<0&&errno==ENOENT;}
static int thread_ok(int pid,int tid){char p[128],b[4096];snprintf(p,sizeof p,"/proc/%d/task/%d/status",pid,tid);if(!text(p,b,sizeof b))return 0;char*t=strstr(b,"TracerPid:");char*s=strstr(b,"State:");unsigned tracer=1;char state=0;return t&&s&&sscanf(t+10,"%u",&tracer)==1&&sscanf(s+6," %c",&state)==1&&!tracer&&state!='T'&&state!='t'&&state!='Z'&&state!='X';}
static int list_threads(struct camera*c,int initial){char p[100];snprintf(p,sizeof p,"/proc/%d/task",c->pid);DIR*d=opendir(p);if(!d)return 0;struct dirent*e;int ok=1;while((e=readdir(d))){char*end;long tid=strtol(e->d_name,&end,10);if(*end||tid<=1||tid>2147483647)continue;unsigned i;for(i=0;i<c->count;i++)if(c->threads[i].tid==(int)tid)break;if(i<c->count)continue;if(c->count>=192||(!initial&&!thread_ok(c->pid,(int)tid))){ok=0;break;}if(initial&&!thread_ok(c->pid,(int)tid)){ok=0;break;}c->threads[c->count++].tid=(int)tid;}closedir(d);return ok&&c->count;}
static int wait_stop(struct thread*t,uint64_t end){while(!cancelled&&ms()<end){int status;pid_t got=waitpid(t->tid,&status,__WALL|WNOHANG);if(got==t->tid){if(!WIFSTOPPED(status))return 0;t->stopped=1;int ours=WSTOPSIG(status)==SIGTRAP&&((unsigned)status>>16)==PTRACE_EVENT_STOP;t->signal=ours?0:WSTOPSIG(status);return ours;}if(got<0&&errno!=EINTR)return 0;struct timespec pause={0,1000000};nanosleep(&pause,NULL);}return 0;}
static int freeze(void*v){struct camera*c=v;uint64_t end=ms()+700u;if(!list_threads(c,1))return 0;
 /* SEIZE/INTERRUPT avoids injecting SIGSTOP into the camera. If the helper
  * exits unexpectedly, the kernel can detach it without a pending group-stop.
  * The pinned camera kernel is Linux3.5; unsupported requests fail closed. */
 for(unsigned round=0;round<4;round++){unsigned before=c->count;for(unsigned i=0;i<c->count;i++){struct thread*t=c->threads+i;if(t->attached)continue;if(ptrace(PTRACE_SEIZE,t->tid,0,0))return 0;t->attached=1;if(ptrace(PTRACE_INTERRUPT,t->tid,0,0)||!wait_stop(t,end))return 0;}if(!list_threads(c,0))return 0;if(c->count==before){
   for(unsigned i=0;i<c->count;i++){uint32_t regs[18];if(ptrace(PTRACE_GETREGS,c->threads[i].tid,0,regs))return 0;if((regs[15]>=0xe4000u&&regs[15]<0xe5000u)||(c->movie_base&&regs[15]>=c->movie_base+0x818u&&regs[15]<c->movie_base+0xa94u))return 0;}return 1;
  }if(ms()>=end)return 0;
 }return 0;
}
static int thaw(void*v){struct camera*c=v;int ok=1;sig_atomic_t saved_cancel=cancelled;cancelled=0;uint64_t end=ms()+700u;for(unsigned i=c->count;i>0;i--){struct thread*t=c->threads+i-1;if(!t->attached)continue;if(!t->stopped){(void)ptrace(PTRACE_INTERRUPT,t->tid,0,0);(void)wait_stop(t,end);}if(ptrace(PTRACE_DETACH,t->tid,0,t->signal)){char path[100];struct stat st;snprintf(path,sizeof path,"/proc/%d/task/%d",c->pid,t->tid);if(errno==ESRCH&&lstat(path,&st)<0&&errno==ENOENT){t->attached=0;continue;}ok=0;}else t->attached=0;}cancelled=saved_cancel;return ok;}
static int write_word(void*v,uint32_t at,uint32_t value){struct camera*c=v;if(at!=VM_HEIGHT&&at!=VM_WIDTH&&(!c->movie_base||(at!=c->movie_base+0xa04u&&at!=c->movie_base+0xa08u)))return 0;for(unsigned i=0;i<c->count;i++)if(c->threads[i].attached&&c->threads[i].stopped)return !ptrace(PTRACE_POKETEXT,c->threads[i].tid,(void*)(uintptr_t)at,(void*)(uintptr_t)value);return 0;}
#include "video_native_profile.inc"
static int lockfile(const char*p){int f=open(p,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return -1;struct stat s;if(fstat(f,&s)||!S_ISREG(s.st_mode)||flock(f,LOCK_EX|LOCK_NB)){close(f);return -1;}return f;}
static void result(const struct camera*c,int ok,const char*reason,const struct vm_result*r){int mode=vm_pair(r->after);printf("NXL_VIDEO_MODE1\nstate=%s\nwidth=%u\nheight=%u\npid=%d\nrecording_or_capture=%d\nreason=%s\nwrites=%u\nrollback_verified=%u\nthreads_detached=%u\nend=NXL_VIDEO_MODE_END1\n",mode==VM_UHD?"UHD":mode==VM_25K?"2K":"UNKNOWN",mode==VM_UHD?3840u:mode==VM_25K?2560u:0u,mode==VM_UHD?2160u:mode==VM_25K?1440u:0u,c->pid,c->mem>=0?busy((struct camera*)c):-1,reason,r->writes,r->rollback_verified,r->thawed);
 FILE*f=fopen("/mnt/mmc/NXDEMO51/logs/video-mode.log","a");if(f){fprintf(f,"VIDEO_MODE pid=%d start=%llu ok=%d reason=%s before=%08x,%08x after=%08x,%08x writes=%u rollback=%u detached=%u\n",c->pid,(unsigned long long)c->start,ok,reason,r->before[0],r->before[1],r->after[0],r->after[1],r->writes,r->rollback_verified,r->thawed);fclose(f);}}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_VIDEO_MODE=R9s; NX500_1.12; NATIVE_UHD30_2997; UHD_DC_PRO80; RO_SHARED_PREFERENCES; GUARDED_PROCESS_RAM; NO_FLASH");return 0;}
 if(argc!=2||(strcmp(argv[1],"status")&&strcmp(argv[1],"2k")&&strcmp(argv[1],"uhd")&&strcmp(argv[1],"30p")&&strcmp(argv[1],"rate70")))return 2;
 signal(SIGINT,stopped);signal(SIGTERM,stopped);signal(SIGPIPE,SIG_IGN);struct camera c={.mem=-1};struct vm_result r={0};int ok=0,mode_lock=-1,run_lock=-1;const char*reason="NX500_FIRMWARE_IDENTITY_FAILED";
 if(!firmware())goto done;c.pid=camera_pid();c.start=identity(c.pid);reason="CAMERA_PROCESS_UNAVAILABLE";if(c.pid<=1||!c.start)goto done;
 char path[100];snprintf(path,sizeof path,"/proc/%d/mem",c.pid);c.mem=open(path,O_RDONLY|O_CLOEXEC);reason="CAMERA_CODE_READ_UNAVAILABLE";if(c.mem<0||!code_context(&c)||!vm_read(&(struct vm_ops){&c,NULL,NULL,NULL,read_word,NULL},r.after))goto done;
 reason="VIDEO_MODE_UNKNOWN_INSTRUCTIONS";if(!vm_pair(r.after))goto done;
 if(!strcmp(argv[1],"status")){ok=1;reason="CURRENT_CONSTRUCTOR_MAPPING";goto done;}
 (void)mkdir("/tmp/nx-lut-lite",0700);mode_lock=lockfile("/tmp/nx-lut-lite/video-mode.fd");reason="VIDEO_MODE_OR_SRW_TASK_BUSY";if(mode_lock<0)goto done;
 run_lock=lockfile("/mnt/mmc/NXDEMO51/run.lock");if(run_lock<0)goto done;
 if(!strcmp(argv[1],"30p")||!strcmp(argv[1],"rate70")){ok=vp_profile(&c,argv[1],&reason);r.writes=vp_writes;r.rollback_verified=vp_rollback;r.thawed=vp_thawed;goto done;}
 struct vm_ops ops={&c,freeze,thaw,ready,read_word,write_word};ok=vm_apply(&ops,!strcmp(argv[1],"2k")?VM_25K:VM_UHD,&r);reason=r.reason;
done:
 if(c.pid>1&&(!strcmp(argv[1],"30p")||!strcmp(argv[1],"rate70")))vp_log(&c,"RESULT",reason);
 if(c.count){int detached=thaw(&c);if(!detached){ok=0;reason="CAMERA_THREAD_DETACH_UNCONFIRMED";}}
 result(&c,ok,reason,&r);if(c.mem>=0)close(c.mem);if(run_lock>=0)close(run_lock);if(mode_lock>=0)close(mode_lock);return ok?0:1;
}
