/* SPDX-License-Identifier: AGPL-3.0-only.
 * Bounded, read-only handoff recorder. No ptrace, MCB, device ioctl, task
 * control, camera/workflow lock or recovery. Only our logger child is signalled.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "still_lifecycle_audit.h"

#define WATCH_SECONDS 1200u
#define WATCH_LIMIT (32u*1024u*1024u)
#define LOG_LIMIT (8u*1024u*1024u)
#define CONSOLE_BASE 0x85300000u
#define CONSOLE_BYTES 0xc0000u
#define CONSOLE_CAPACITY 0xbfff8u
static volatile sig_atomic_t stop_requested;
static int directory=-1,dev=-1,camera,logger=-1,log_pipe=-1;
static FILE *events,*dlog,*console;
static unsigned dlog_bytes,console_bytes,ram_have;
static volatile unsigned char *ring;
static uint32_t ring_pos;
static uint64_t started;
struct page {uint32_t base;volatile unsigned char *memory;};
static struct page pages[256];static unsigned page_count;
static struct thread_seen {int tid;uint32_t hash;} seen[128];
static uint32_t ram_hash;
static void blocked_sample(int tid,const char *text,unsigned sequence);
static const uint32_t ram_addresses[]={
 0x8153e6d4u,0x8153f3f8u,0x81540f38u,0x8153cbdcu,0x8153cbe0u,0x8153cbe4u,
 0x8153f358u,0x8153f35cu,0x8153f3a8u,0x8153f3acu,
 0x8153f438u,0x8153f43cu,0x8153f440u,0x81543d40u,
 0x8153f410u,0x8153f414u,0x8153f418u,0x8153f41cu,0x8153f420u,0x8153f424u,
 0x8142d154u,0x8142de78u,0x8142dec8u,0x8142deccu,
 0x867551c4u,0x86755238u,0x867554c4u,0x867555c4u,
 0x8671d81cu,0x8671d820u,0x8671d824u,
 /* Three raw completion words per native ACC selector, including13. */
 0x867554c8u,0x867554ccu,0x867554d0u,
 0x867554e0u,0x867554e4u,0x867554e8u,
 0x867554ecu,0x867554f0u,0x867554f4u,
 0x86755504u,0x86755508u,0x8675550cu,
 0x86755564u,0x86755568u,0x8675556cu,
 0x8142aae8u,0x8142afdcu,0x813ebf48u,0x813ebf4cu,
 0x81eac90cu,0x81eac910u,0x81eacda0u,
 0x817d7b48u,0x817d7b4cu,0x817d7b50u,0x817d7b54u,
 0x817d7b90u,0x817d7b94u,0x817d7b98u,0x817d7b9cu,
 0x81792ab8u,0x817900e0u,0x81791710u
};
static void stopped(int s){(void)s;stop_requested=1;}
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)){stop_requested=1;return 0;}return (uint64_t)t.tv_sec*1000000000u+t.tv_nsec;}
static void close_inherited(void){
 long limit=sysconf(_SC_OPEN_MAX);if(limit<0||limit>65536)limit=65536;
 for(int f=3;f<limit;f++)close(f);
 /* A recorder outlives its submitting shell; it must never retain that
  * round's file locks, pipe endpoints, private descriptors or lease token. */
 unsetenv("NX51_SESSION_TOKEN");unsetenv("NX51_SESSION_ROOT");unsetenv("NX51_JOB");
}
static uint32_t hash_bytes(const void *p,size_t n){const unsigned char *b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int read_text(const char *path,char *out,size_t capacity){
 if(capacity<2)return 0;out[0]=0;int f=open(path,O_RDONLY|O_CLOEXEC);if(f<0)return 0;
 size_t used=0;ssize_t n=0;
 while(used<capacity-1){
  do{n=read(f,out+used,capacity-1-used);}while(n<0&&errno==EINTR&&!stop_requested);
  if(n<=0)break;used+=(size_t)n;
 }
 int saved=errno;close(f);errno=saved;out[used]=0;return n>=0;
}
static FILE *new_file(const char *name){int f=openat(directory,name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return NULL;FILE *p=fdopen(f,"w");if(!p)close(f);return p;}
static int durable(FILE *f){return f&&!ferror(f)&&!fflush(f)&&!fsync(fileno(f));}
static void quoted(FILE *f,const char *s){
 fputc('"',f);for(const unsigned char *p=(const unsigned char*)s;*p;p++){
  unsigned c=*p;if(c=='"'||c=='\\'){fputc('\\',f);fputc(c,f);}else if(c>=32&&c<127)fputc(c,f);else fprintf(f,"\\u%04x",c);
 }fputc('"',f);
}
static int camera_pid(void){
 DIR *d=opendir("/proc");if(!d)return 0;struct dirent *e;int result=0;
 while((e=readdir(d))){char *end;long p=strtol(e->d_name,&end,10);if(*end||p<=1||p>2147483647)continue;
  char path[96],target[256];snprintf(path,sizeof path,"/proc/%ld/exe",p);ssize_t n=readlink(path,target,sizeof target-1);
  if(n<0||n>=(ssize_t)sizeof target-1)continue;target[n]=0;
  if(!strcmp(target,"/usr/apps/com.samsung.di-camera-app/bin/di-camera-app")){if(result){result=0;break;}result=(int)p;}
 }closedir(d);return result;
}
static int word(uint32_t address,uint32_t *out){
 /* Ordinary DDR only. Never map ISP registers or read clock-gated MMIO. */
 if(dev<0||(address&3u)||address<0x81000000u||address>0x93fffffcu)return 0;
 uint32_t base=address&~4095u;unsigned i;for(i=0;i<page_count;i++)if(pages[i].base==base)break;
 if(i==page_count){if(page_count==256)return 0;void *m=mmap(NULL,4096,PROT_READ,MAP_SHARED,dev,(off_t)(uint64_t)base);if(m==MAP_FAILED)return 0;pages[i].base=base;pages[i].memory=m;page_count++;}
 *out=*(const volatile uint32_t*)(pages[i].memory+(address&4095u));return 1;
}
static void ram_sample(unsigned sample){
 uint32_t values[sizeof ram_addresses/sizeof *ram_addresses],available[sizeof ram_addresses/sizeof *ram_addresses];
 for(unsigned i=0;i<sizeof values/sizeof *values;i++){values[i]=0;available[i]=(unsigned)word(ram_addresses[i],values+i);}
 uint32_t h=hash_bytes(values,sizeof values)^hash_bytes(available,sizeof available);
 if(ram_have&&h==ram_hash&&sample%10)return;ram_have=1;ram_hash=h;
 fprintf(events,"{\"event\":\"ram\",\"sample\":%u,\"mono_ns\":%llu,\"atomic\":false,\"words\":[",sample,(unsigned long long)now());
 for(unsigned i=0;i<sizeof values/sizeof *values;i++){fprintf(events,"%s[%u,",i?",":"",ram_addresses[i]);if(available[i])fprintf(events,"%u",values[i]);else fputs("null",events);fputc(']',events);}fputs("]}\n",events);
}
static uint64_t process_identity(int pid){
 char path[80],text[2048];snprintf(path,sizeof path,"/proc/%d/stat",pid);if(!read_text(path,text,sizeof text))return 0;
 char *p=strrchr(text,')');if(!p||p[1]!=' ')return 0;p+=2;
 for(unsigned field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;while(*p==' ')p++;}
 return strtoull(p,NULL,10);
}
static void thread_sample(unsigned sample){
 char root[80];snprintf(root,sizeof root,"/proc/%d/task",camera);DIR *d=opendir(root);if(!d)return;struct dirent *e;unsigned visited=0;
 while((e=readdir(d))&&visited<128){char *end;long tid=strtol(e->d_name,&end,10);if(*end||tid<=1||tid>2147483647)continue;visited++;
  char path[128],comm[64]={0},stat[2048]={0},syscall[512]={0},wchan[160]={0},stack[1024]={0};
#define GET(name,buffer) do{snprintf(path,sizeof path,"/proc/%d/task/%ld/" name,camera,tid);read_text(path,buffer,sizeof buffer);}while(0)
  GET("comm",comm);GET("stat",stat);GET("syscall",syscall);GET("wchan",wchan);GET("stack",stack);
#undef GET
  char *s=strrchr(stat,')');char state=s&&s[1]==' '?s[2]:'?';
  uint32_t h=hash_bytes(comm,strlen(comm))^hash_bytes(syscall,strlen(syscall))^hash_bytes(wchan,strlen(wchan))^hash_bytes(stack,strlen(stack))^(unsigned char)state;
  unsigned slot;for(slot=0;slot<128;slot++)if(seen[slot].tid==(int)tid||!seen[slot].tid)break;
  if(slot<128&&seen[slot].tid==(int)tid&&seen[slot].hash==h&&sample%10)continue;
  if(slot<128){seen[slot].tid=(int)tid;seen[slot].hash=h;}
  fprintf(events,"{\"event\":\"thread\",\"sample\":%u,\"mono_ns\":%llu,\"tid\":%ld,\"state\":\"%c\",\"comm\":",sample,(unsigned long long)now(),tid,state);
  quoted(events,comm);fputs(",\"syscall\":",events);quoted(events,syscall);fputs(",\"wchan\":",events);quoted(events,wchan);fputs(",\"stack\":",events);quoted(events,stack);fputs("}\n",events);
  blocked_sample((int)tid,syscall,sample*250u);
 }closedir(d);
}
static void fd_sample(unsigned sample){
 if(sample%10)return;char path[96];snprintf(path,sizeof path,"/proc/%d/fd",camera);DIR *d=opendir(path);if(!d)return;struct dirent *e;unsigned count=0;
 fprintf(events,"{\"event\":\"fds\",\"sample\":%u,\"mono_ns\":%llu,\"entries\":[",sample,(unsigned long long)now());
 while((e=readdir(d))&&count<256){char *end;long fd=strtol(e->d_name,&end,10);if(*end||fd<0)continue;char link[160],target[384];snprintf(link,sizeof link,"/proc/%d/fd/%ld",camera,fd);ssize_t n=readlink(link,target,sizeof target-1);if(n<0)continue;target[n]=0;
  if(strncmp(target,"/dev/",5))continue;fprintf(events,"%s[%ld,",count?",":"",fd);quoted(events,target);fputc(']',events);count++;
 }fputs("]}\n",events);closedir(d);
 char mem[4096];if(read_text("/proc/meminfo",mem,sizeof mem)){fprintf(events,"{\"event\":\"meminfo\",\"sample\":%u,\"text\":",sample);quoted(events,mem);fputs("}\n",events);}
}
static void ring_sample(void){
 if(!ring||console_bytes>=LOG_LIMIT)return;uint64_t begin=now();unsigned before_bytes=console_bytes;uint32_t before_pos=ring_pos;
 uint32_t after=*(const volatile uint32_t*)(ring+4);if(after>=CONSOLE_CAPACITY)return;
 unsigned n=after>=ring_pos?after-ring_pos:CONSOLE_CAPACITY-ring_pos+after;
 if(n>65536u){fprintf(events,"{\"event\":\"console_gap\",\"bytes\":%u}\n",n);ring_pos=after;return;}
 while(n--&&console_bytes<LOG_LIMIT){unsigned char c=ring[8+ring_pos];if(c){fputc(c,console);console_bytes++;}if(++ring_pos==CONSOLE_CAPACITY)ring_pos=0;}
 ring_pos=after;
 if(console_bytes!=before_bytes)fprintf(events,"{\"event\":\"console_chunk\",\"read_begin_ns\":%llu,\"read_end_ns\":%llu,\"offset\":%u,\"bytes\":%u,\"cursor_before\":%u,\"cursor_after\":%u,\"drain_time_not_event_time\":true}\n",(unsigned long long)begin,(unsigned long long)now(),before_bytes,console_bytes-before_bytes,before_pos,after);
}
static void logger_start(void){
 int pipefd[2];if(pipe2(pipefd,O_CLOEXEC|O_NONBLOCK))return;pid_t p=fork();if(p<0){close(pipefd[0]);close(pipefd[1]);return;}
 if(!p){
  if(prctl(PR_SET_PDEATHSIG,SIGTERM)||getppid()==1)_exit(127);
  signal(SIGTERM,SIG_DFL);signal(SIGHUP,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGPIPE,SIG_DFL);
  int flags=fcntl(pipefd[1],F_GETFL);if(flags>=0)fcntl(pipefd[1],F_SETFL,flags&~O_NONBLOCK);
  if(dup2(pipefd[1],STDOUT_FILENO)<0)_exit(127);int null=open("/dev/null",O_RDWR);if(null>=0){dup2(null,STDIN_FILENO);dup2(null,STDERR_FILENO);}
  /* All recorder descriptors are CLOEXEC, including its read-only maps fd. */
  execlp("dlogutil","dlogutil","-v","brief",(char*)NULL);_exit(127);
 }
 close(pipefd[1]);logger=p;log_pipe=pipefd[0];
}
static void logger_sample(void){
 if(log_pipe<0)return;uint64_t begin=now();unsigned before_bytes=dlog_bytes;char b[8192];for(unsigned i=0;i<16;i++){ssize_t n=read(log_pipe,b,sizeof b);if(n<=0)break;
  unsigned keep=(unsigned)n;if(keep>LOG_LIMIT-dlog_bytes)keep=LOG_LIMIT-dlog_bytes;if(keep){fwrite(b,1,keep,dlog);dlog_bytes+=keep;}
 }
 if(dlog_bytes!=before_bytes)fprintf(events,"{\"event\":\"dlog_chunk\",\"read_begin_ns\":%llu,\"read_end_ns\":%llu,\"offset\":%u,\"bytes\":%u,\"drain_time_not_event_time\":true}\n",(unsigned long long)begin,(unsigned long long)now(),before_bytes,dlog_bytes-before_bytes);
}
static void logger_stop(void){
 if(logger>1){int status;pid_t r=waitpid(logger,&status,WNOHANG);if(r==0){kill(logger,SIGTERM);for(unsigned i=0;i<50;i++){r=waitpid(logger,&status,WNOHANG);if(r!=0)break;struct timespec t={0,20000000};nanosleep(&t,NULL);}if(!r){kill(logger,SIGKILL);while(waitpid(logger,&status,0)<0&&errno==EINTR){}}}}
 if(log_pipe>=0)close(log_pipe);log_pipe=-1;logger=-1;
}
#include "handoff_broad_observer.inc"
int main(int argc,char **argv){
 if(argc!=2||!observer_path(argv[1]))return 2;
 close_inherited();
 /* This lease belongs only to the passive observer. It is independent of
  * session/run/engine/trace locks and disappears automatically on exit. */
 int observer_lease=open("/tmp/nx-lut-lite/handoff-observer.fd",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
 if(observer_lease<0)return 2;if(flock(observer_lease,LOCK_EX|LOCK_NB)){close(observer_lease);return 0;}
 directory=open(argv[1],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(directory<0)return 2;
 events=new_file("context.jsonl");dlog=new_file("camera-dlog.txt");console=new_file("rtos-console.txt");if(!events||!dlog||!console)return 2;
 signal(SIGTERM,stopped);signal(SIGINT,stopped);signal(SIGHUP,SIG_IGN);signal(SIGPIPE,SIG_IGN);
 started=now();camera=camera_pid();uint64_t identity=process_identity(camera);char boot[80]={0};read_text("/proc/sys/kernel/random/boot_id",boot,sizeof boot);
 dev=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
 if(dev>=0){void *m=mmap(NULL,CONSOLE_BYTES,PROT_READ,MAP_SHARED,dev,(off_t)(uint64_t)CONSOLE_BASE);if(m!=MAP_FAILED){ring=m;ring_pos=*(const volatile uint32_t*)(ring+4);if(ring_pos>=CONSOLE_CAPACITY){munmap(m,CONSOLE_BYTES);ring=NULL;}else if(ring_pos>65536u)ring_pos-=65536u;else ring_pos=0;}}
 fprintf(events,"{\"format\":\"NXHANDOFFWATCH1\",\"version\":\"R7h-diagnostic\",\"camera_pid\":%d,\"camera_start_ticks\":%llu,\"start_mono_ns\":%llu,\"boot\":",camera,(unsigned long long)identity,(unsigned long long)started);quoted(events,boot);
 fprintf(events,",\"profile\":\"NX500_1.12\",\"devmem_readonly\":%s,\"mmio_access\":false,\"native_calls\":0,\"ptrace_calls\":0,\"device_writes\":0,\"atomic_snapshot\":false}\n",dev>=0?"true":"false");
 if(!camera||!identity||!started||!durable(events)||!broad_begin())return 2;logger_start();
 FILE *ready=new_file("READY");if(!ready)return 2;fprintf(ready,"NXHANDOFFWATCH1\npid=%d\ncamera_pid=%d\nreadonly=1\nmax_seconds=%u\n",getpid(),camera,WATCH_SECONDS);int ready_ok=durable(ready);if(fclose(ready)||!ready_ok||fsync(directory))return 2;
 photo_inventory("begin");
 int active=open("/tmp/nx-lut-lite/handoff-observer.active",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
 if(active>=0){dprintf(active,"%d\n%s\n",getpid(),argv[1]);fsync(active);close(active);}
 const char *reason="DURATION_REACHED";unsigned sample=0;uint64_t next_slow=started,next_ui=started,next_linux=started,next_context=started;
 for(;!stop_requested&&now()-started<(uint64_t)WATCH_SECONDS*1000000000u;sample++){
  hot_sample(sample);
  uint64_t stamp=now();if(stamp>=next_ui){ui_wait_sample(sample);next_ui=stamp+20000000u;}
  if(stamp>=next_linux){linux_sample(sample);next_linux=stamp+50000000u;}
  if(stamp>=next_context){context_sample(sample);typed_sample(sample);next_context=stamp+20000000u;}
  allocator_sample(sample);
  if(stamp>=next_slow){
  struct stat marker;if(!fstatat(directory,"STOP",&marker,AT_SYMLINK_NOFOLLOW)){reason="STOP_MARKER";break;}
  if(process_identity(camera)!=identity){reason="CAMERA_PROCESS_EXITED_OR_CHANGED";break;}
  unsigned slow=sample/250u;ram_sample(slow);thread_sample(slow);fd_sample(slow);ring_sample();logger_sample();maps_sample(slow);
  if(!durable(events)||!durable(dlog)||!durable(console)||!broad_flush()){reason="OUTPUT_ERROR";break;}
  if(ftello(events)>WATCH_LIMIT){reason="CONTEXT_SIZE_LIMIT";break;}
  next_slow=now()+1000000000u;
  }
  uint64_t finish=now(),target=stamp+4000000u;if(finish<target){uint64_t delay_ns=target-finish;struct timespec delay={0,(long)delay_ns};nanosleep(&delay,NULL);}
 }
 if(stop_requested)reason="SIGNAL";logger_sample();logger_stop();ring_sample();broad_end();photo_inventory("end");
 fprintf(events,"{\"event\":\"end\",\"reason\":\"%s\",\"mono_ns\":%llu,\"samples\":%u,\"dlog_bytes\":%u,\"console_bytes\":%u}\n",reason,(unsigned long long)now(),sample,dlog_bytes,console_bytes);
 int ok=durable(events)&&durable(dlog)&&durable(console);int ce=fclose(events),cd=fclose(dlog),cc=fclose(console);if(ce||cd||cc)ok=0;
 if(ring)munmap((void*)ring,CONSOLE_BYTES);for(unsigned i=0;i<page_count;i++)munmap((void*)pages[i].memory,4096);if(dev>=0)close(dev);
 FILE *done=new_file("DONE");if(done){fprintf(done,"NXHANDOFFWATCHDONE1\nreason=%s\nsamples=%u\n",reason,sample);int saved=durable(done);if(fclose(done)||!saved)ok=0;}else ok=0;
 if(fsync(directory)||close(directory))ok=0;close(observer_lease);return ok?0:1;
}
