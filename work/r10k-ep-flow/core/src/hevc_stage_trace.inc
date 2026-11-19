/* SPDX-License-Identifier: AGPL-3.0-only. Pinned NX5001.12 ARM user code.
 * Five hardware execution breakpoints on the unique native videoencoder TID.
 * An unsupported kernel never falls back to software breakpoints/text writes.
 */
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/time.h>
#ifndef PTRACE_GETHBPREGS
#define PTRACE_GETHBPREGS 29
#define PTRACE_SETHBPREGS 30
#endif
struct ht_regs {uint32_t r[18];};
struct ht_worker {
 int pid,tid,mem,attached,stopped,pending_signal,modified;uint64_t parent_start,camera_start;
 uint32_t bases[2],address[5],saved[10],request;
 struct ht_record current,releasing;struct ht_header*h;struct ht_record*records;
};
static volatile sig_atomic_t ht_pulse;
static void ht_tick(int sig){(void)sig;ht_pulse=1;}
static void ht_u64(uint32_t*w,uint64_t n){w[0]=(uint32_t)n;w[1]=(uint32_t)(n>>32);}
static int ht_read(struct ht_worker*c,uint32_t a,void*out,unsigned n){return vp_user(a,n)&&n<=128u&&pread(c->mem,out,n,(off_t)(uint64_t)a)==(ssize_t)n;}
static int ht_hbp(struct ht_worker*c,int write,unsigned n,uint32_t*v){return ptrace(write?PTRACE_SETHBPREGS:PTRACE_GETHBPREGS,c->tid,(void*)(uintptr_t)n,v)==0;}
static int ht_wait(struct ht_worker*c,uint64_t deadline){
 while(!interrupted&&clock_ns(CLOCK_MONOTONIC)<deadline){int status;int got=waitpid(c->tid,&status,__WALL);if(got==c->tid){if(!WIFSTOPPED(status)){if(WIFEXITED(status)||WIFSIGNALED(status)){c->attached=c->stopped=0;c->h->w[12]=1;}return 0;}c->stopped=1;c->pending_signal=WSTOPSIG(status);return status;}if(got<0&&errno!=EINTR)return 0;}
 return 0;
}
static int ht_controls(struct ht_worker*c,int enabled){for(unsigned i=0;i<5;i++){uint32_t v=enabled?0x1e5u:0x1e4u;if(!ht_hbp(c,1,2*i+2,&v))return 0;}return 1;}
static int ht_detach(struct ht_worker*c){
 if(!c->attached)return 1;
 int ok=1;if(!c->stopped){if(ptrace(PTRACE_INTERRUPT,c->tid,0,0))ok=0;else{sig_atomic_t stop=interrupted;interrupted=0;int s=ht_wait(c,clock_ns(CLOCK_MONOTONIC)+1000000000u);interrupted=stop;if(!s)ok=0;else if((unsigned)s>>16==PTRACE_EVENT_STOP)c->pending_signal=0;}}
 if(c->stopped){if(c->modified)for(unsigned i=0;i<10;i++){uint32_t v=c->saved[i];if((i&1u)&&!v)v=0x1e4u;if(!ht_hbp(c,1,i+1,&v))ok=0;}if(ptrace(PTRACE_DETACH,c->tid,0,(void*)(uintptr_t)c->pending_signal))ok=0;else c->attached=c->stopped=c->modified=0;}
 c->h->w[12]=ok?1u:0u;return ok;
}
static int ht_thread(int pid){
 char p[128],name[64],status[2048];snprintf(p,sizeof p,"/proc/%d/task",pid);DIR*d=opendir(p);if(!d)return 0;int found=0;struct dirent*e;
 while((e=readdir(d))){
  char*end;long n=strtol(e->d_name,&end,10);if(*end||n<=1)continue;
  snprintf(p,sizeof p,"/proc/%d/task/%ld/comm",pid,n);if(!read_text(p,name,sizeof name))continue;
  name[strcspn(name,"\r\n")]=0;if(strcmp(name,"videoencoder"))continue;
  if(found){found=0;break;}
  snprintf(p,sizeof p,"/proc/%d/task/%ld/status",pid,n);unsigned tracer=1;char*q,*state;char code=0;
  if(!read_text(p,status,sizeof status)||(state=strstr(status,"State:"))==NULL||sscanf(state+6," %c",&code)!=1||
     (code!='S'&&code!='R')||(q=strstr(status,"TracerPid:"))==NULL||sscanf(q+10,"%u",&tracer)!=1||tracer){found=0;break;}
  found=(int)n;
 }closedir(d);return found;
}
static int ht_append(struct ht_worker*c,struct ht_record*z){
 uint32_t n=c->h->w[4];if(n>=HT_CAPACITY){c->h->w[6]|=16u;return 0;}z->sequence=n;z->commit=vp_hash(z,offsetof(struct ht_record,commit))^HT_XOR;c->records[n]=*z;__sync_synchronize();c->h->w[4]=n+1;return 1;
}
static int ht_event(struct ht_worker*c,unsigned point,const struct ht_regs*g,struct ht_record*z){
 memset(z,0,sizeof*z);z->ns=clock_ns(CLOCK_MONOTONIC);z->kind=point+1;z->pc=g->r[15];z->status=g->r[0];z->object=g->r[4];
 if(point<3){uint32_t type=0,q=0,planes[4];
  if(!ht_read(c,g->r[4],&type,4)||type!=c->bases[0]+0x4c9c8u||!ht_read(c,g->r[4]+0x108u,&q,4)||!ht_read(c,q,planes,16))return 0;
  z->descriptor=q;z->y=planes[2];z->uv=planes[3];
  if(point==0){c->request++;if(!c->request)c->request++;z->request=c->request;c->current=*z;}
  else if(c->current.object==z->object&&c->current.y==z->y&&c->current.uv==z->uv)z->request=c->current.request;
 }else if(point==3){uint32_t type=0,p=0,words[14];if(!ht_read(c,g->r[4],&type,4)||type!=c->bases[1]+0x59da8u||!ht_read(c,g->r[6],&p,4)||!ht_read(c,p,words,sizeof words))return 0;z->descriptor=p;z->y=words[11];z->uv=words[13];if(c->current.y==z->y&&c->current.uv==z->uv)z->request=c->current.request;c->releasing=*z;}
 else{z->descriptor=c->releasing.descriptor;z->y=c->releasing.y;z->uv=c->releasing.uv;z->request=c->releasing.request;if(c->releasing.object!=z->object)return 0;}
 if(!z->y||!z->uv||(z->y&15u)||(z->uv&15u))return 0;z->flags=1u;return 1;
}
static int ht_arm(struct ht_worker*c){
 if(ticks(c->pid)!=c->camera_start)return 0;
 c->tid=ht_thread(c->pid);if(!c->tid)return 0;c->h->w[8]=(unsigned)c->tid;c->h->w[12]=0;memset(&c->current,0,sizeof c->current);memset(&c->releasing,0,sizeof c->releasing);
 if(ptrace(PTRACE_SEIZE,c->tid,0,0))return 0;c->attached=1;
 if(ptrace(PTRACE_INTERRUPT,c->tid,0,0))return 0;
 int status=ht_wait(c,clock_ns(CLOCK_MONOTONIC)+1000000000u);if(!status||((unsigned)status>>16)!=PTRACE_EVENT_STOP)return 0;c->pending_signal=0;
 uint32_t info=0;if(!ht_hbp(c,0,0,&info))return 0;c->h->w[9]=info&255u;if(c->h->w[9]<5u)return 0;
 for(unsigned i=0;i<10;i++)if(!ht_hbp(c,0,i+1,c->saved+i))return 0;
 for(unsigned i=0;i<5;i++)if(c->saved[2*i+1]&1u)return 0;
 /* Every observed instruction is linear. Kernel-managed ARM single-step
  * therefore uses only the unique videoencoder path, never a shared PLT. */
 static const uint32_t offsets[5]={0x2cc50u,0x2cc74u,0x2cc94u,0x38434u,0x3844cu};
 static const uint32_t expected[5]={0xe3071408u,0xe3500000u,0xe5943130u,0xe5953000u,0xe3a00000u};
 for(unsigned i=0;i<5;i++){
  c->address[i]=c->bases[i<3?0:1]+offsets[i];uint32_t word=0;if(!ht_read(c,c->address[i],&word,4)||word!=expected[i])return 0;
  uint32_t value=c->address[i];c->modified=1;if(!ht_hbp(c,1,2*i+1,&value))return 0;
 }
 if(!ht_controls(c,1))return 0;
 for(unsigned i=0;i<5;i++){uint32_t a=0,b=0;if(!ht_hbp(c,0,2*i+1,&a)||!ht_hbp(c,0,2*i+2,&b)||a!=c->address[i]||b!=0x1e5u)return 0;}
 c->pending_signal=0;if(ptrace(PTRACE_CONT,c->tid,0,0))return 0;c->stopped=0;return 1;
}
static int ht_window(struct ht_worker*c,uint64_t until){
 while(!interrupted&&clock_ns(CLOCK_MONOTONIC)<until){
  int status=ht_wait(c,until);if(!status)break;
  int sig=WSTOPSIG(status);if(sig!=SIGTRAP){if(ptrace(PTRACE_CONT,c->tid,0,(void*)(uintptr_t)sig))return 0;c->stopped=0;continue;}
  struct ht_regs g;if(ptrace(PTRACE_GETREGS,c->tid,0,&g))return 0;unsigned point=5;for(unsigned i=0;i<5;i++)if(g.r[15]==c->address[i]){point=i;break;}
  siginfo_t info;if(ptrace(PTRACE_GETSIGINFO,c->tid,0,&info))return 0;int owned=point<5;for(unsigned i=0;i<5;i++)owned|=(uint32_t)(uintptr_t)info.si_addr==c->address[i];
  if(info.si_code!=4||!owned){c->pending_signal=SIGTRAP;return 0;}c->pending_signal=0;if(point==5)return 0;
  uint64_t stopped_ns=clock_ns(CLOCK_MONOTONIC);struct ht_record z;int valid=ht_event(c,point,&g,&z);c->h->w[11]++;
  if(!ht_controls(c,0)||ptrace(PTRACE_SINGLESTEP,c->tid,0,0))return 0;c->stopped=0;c->pending_signal=0;
  status=ht_wait(c,stopped_ns+20000000u);struct ht_regs advanced;
  if(!status||WSTOPSIG(status)!=SIGTRAP||ptrace(PTRACE_GETSIGINFO,c->tid,0,&info))return 0;
  if(info.si_code!=1&&info.si_code!=2)return 0;c->pending_signal=0;
  if(ptrace(PTRACE_GETREGS,c->tid,0,&advanced)||advanced.r[15]!=g.r[15]+4u)return 0;
  uint64_t span=clock_ns(CLOCK_MONOTONIC)-stopped_ns;uint64_t old=(uint64_t)c->h->w[22]|((uint64_t)c->h->w[23]<<32);if(span>old)ht_u64(c->h->w+22,span);
  if(span>8000000u){c->h->w[6]|=32u;return 0;}
  if(!ht_controls(c,1))return 0;z.resume_ns=clock_ns(CLOCK_MONOTONIC);
  if(valid&&!ht_append(c,&z))return 0;if(!valid)c->h->w[26]++;
  if(ptrace(PTRACE_CONT,c->tid,0,0))return 0;c->stopped=0;c->pending_signal=0;
 }
 return 1;
}
static void ht_run(void*map,int pid,uint64_t camera_start,const uint32_t*bases,int parent,uint64_t parent_start){
 close_inherited();interrupted=0;signal(SIGINT,stopped);signal(SIGTERM,stopped);signal(SIGHUP,stopped);
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=ht_tick;sigemptyset(&sa.sa_mask);sigaction(SIGALRM,&sa,NULL);
 struct itimerval timer={{0,100000},{0,100000}};setitimer(ITIMER_REAL,&timer,NULL);
 struct ht_worker c;memset(&c,0,sizeof c);c.pid=pid;c.camera_start=camera_start;c.mem=-1;c.bases[0]=bases[0];c.bases[1]=bases[1];c.h=(struct ht_header*)((unsigned char*)map+HT_OFFSET);c.records=(struct ht_record*)((unsigned char*)map+HT_DATA);
 c.h->w[0]=HT_MAGIC;c.h->w[1]=1;c.h->w[2]=sizeof(struct ht_record);c.h->w[3]=HT_CAPACITY;c.h->w[5]=1;c.h->w[7]=(unsigned)getpid();c.h->w[12]=1;c.h->w[13]=(unsigned)parent;c.h->w[14]=(unsigned)pid;
 ht_u64(c.h->w+16,clock_ns(CLOCK_MONOTONIC));uint64_t cpu=clock_ns(CLOCK_PROCESS_CPUTIME_ID),since=0,next=0;char path[80];snprintf(path,sizeof path,"/proc/%d/mem",pid);c.mem=open(path,O_RDONLY|O_CLOEXEC);
 if(c.mem<0){c.h->w[6]|=1u;goto done;}
 while(!interrupted&&ticks(parent)==parent_start&&ticks(pid)==camera_start){
  uint64_t now=clock_ns(CLOCK_MONOTONIC);int busy=camera_busy(pid);
  if(busy!=1){since=0;next=0;struct timespec t={0,100000000};nanosleep(&t,NULL);continue;}
  if(!since){since=now;next=now+3000000000u;}
  if(now<next){struct timespec t={0,100000000};nanosleep(&t,NULL);continue;}
  if(!ht_arm(&c)){c.h->w[5]=4;c.h->w[6]|=2u;ht_detach(&c);goto done;}
  c.h->w[5]=2;c.h->w[10]++;struct ht_record z;memset(&z,0,sizeof z);z.ns=clock_ns(CLOCK_MONOTONIC);z.kind=HT_WINDOW_ON;ht_append(&c,&z);
  uint64_t window_cpu=clock_ns(CLOCK_PROCESS_CPUTIME_ID),window_start=clock_ns(CLOCK_MONOTONIC);
  int ok=ht_window(&c,window_start+3000000000u);int detached=ht_detach(&c);
  if((clock_ns(CLOCK_PROCESS_CPUTIME_ID)-window_cpu)*20u>clock_ns(CLOCK_MONOTONIC)-window_start){c.h->w[6]|=64u;ok=0;}
  memset(&z,0,sizeof z);z.ns=clock_ns(CLOCK_MONOTONIC);z.kind=HT_WINDOW_OFF;z.flags=detached?1u:0u;ht_append(&c,&z);
  if(!ok||!detached){c.h->w[5]=5;c.h->w[6]|=4u;goto done;}
  c.h->w[5]=1;next=clock_ns(CLOCK_MONOTONIC)+3000000000u;
 }
done:
 if(c.attached&&!ht_detach(&c))c.h->w[6]|=8u;
 if(c.mem>=0)close(c.mem);if(c.h->w[5]<4)c.h->w[5]=3;
 ht_u64(c.h->w+18,clock_ns(CLOCK_MONOTONIC));ht_u64(c.h->w+20,clock_ns(CLOCK_PROCESS_CPUTIME_ID)-cpu);c.h->w[31]=vp_hash(c.h,124);__sync_synchronize();_exit(c.h->w[6]?1:0);
}
static int ht_stop_worker(pid_t*child,struct ht_header*h){
 if(*child<=0)return 1;int status;pid_t got=waitpid(*child,&status,WNOHANG);
 if(!got){kill(*child,SIGTERM);uint64_t deadline=clock_ns(CLOCK_MONOTONIC)+2500000000u;while(clock_ns(CLOCK_MONOTONIC)<deadline){got=waitpid(*child,&status,WNOHANG);if(got==*child)break;if(got<0&&errno!=EINTR)break;struct timespec t={0,10000000};nanosleep(&t,NULL);}}
 if(got!=*child){h->w[6]|=128u;kill(*child,SIGKILL);while(waitpid(*child,&status,0)<0&&errno==EINTR){}h->w[12]=0;}
 *child=-1;return h->w[12]!=0;
}
