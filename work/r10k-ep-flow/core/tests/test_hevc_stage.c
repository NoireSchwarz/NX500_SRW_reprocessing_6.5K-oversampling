#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>
#include <time.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include "../../common/video_counter_capture.h"
#include "../../common/video_fast_capture.h"
#include "../../common/video_hevc_trace.h"
#define CHECK(x) do{if(!(x)){printf("FAIL %d %s\n",__LINE__,#x);return 1;}}while(0)
static volatile sig_atomic_t interrupted;
static uint64_t wall=1000000000u;
static int task_count=1,foreign_tracer,resources=5,attached,stopped_state,waiting,step_event,event_index,detach_signal,writes,fail_write,large_stop,wrong_pc,exit_wait;
static char task_state='S';static uint64_t camera_ticks=1;
static uint32_t bp[10],current_pc;
static const uint32_t points[5]={0x1002cc50,0x1002cc74,0x1002cc94,0x11038434,0x1103844c};
static uint64_t clock_ns(clockid_t id){(void)id;wall+=1000;return wall;}
static void stopped(int sig){(void)sig;interrupted=1;}
static void close_inherited(void){}
static uint64_t ticks(int pid){(void)pid;return camera_ticks;}
static int camera_busy(int pid){(void)pid;return 0;}
static int read_text(const char*p,char*b,unsigned n){
 if(strstr(p,"/comm")){snprintf(b,n,"videoencoder\n");return 1;}
 if(strstr(p,"/status")){snprintf(b,n,"State:\t%c\nTracerPid:\t%d\n",task_state,foreign_tracer);return 1;}return 0;
}
static DIR*model_opendir(const char*p){(void)p;waiting=0;return(DIR*)1;}
static struct dirent*model_readdir(DIR*d){(void)d;static struct dirent e;if(waiting++>=task_count)return NULL;snprintf(e.d_name,sizeof e.d_name,"%d",310+waiting);return &e;}
static int model_closedir(DIR*d){(void)d;return 0;}
static ssize_t model_pread(int fd,void*out,size_t n,off_t offset){
 (void)fd;uint32_t a=(uint32_t)offset,v[32]={0};
 for(unsigned i=0;i<5;i++)if(a==points[i]&&n==4){static const uint32_t words[5]={0xe3071408,0xe3500000,0xe5943130,0xe5953000,0xe3a00000};v[0]=words[i];memcpy(out,v,4);return 4;}
 if(a==0x10010000&&n==4)v[0]=0x1004c9c8;
 else if(a==0x10010108&&n==4)v[0]=0x10020000;
 else if(a==0x10020000&&n==16){v[2]=0x9a000000;v[3]=0x9a7e9000;}
 else if(a==0x11020000&&n==4)v[0]=0x11059da8;
 else if(a==0x11010000&&n==4)v[0]=0x11030000;
 else if(a==0x11030000&&n==56){v[11]=0x9a000000;v[13]=0x9a7e9000;}
 else return -1;
 memcpy(out,v,n);return(ssize_t)n;
}
static long model_ptrace(int request,...){
 va_list ap;va_start(ap,request);int tid=va_arg(ap,int);void*addr=va_arg(ap,void*);void*data=va_arg(ap,void*);va_end(ap);(void)tid;
 switch(request){
 case PTRACE_SEIZE:if(attached)return-1;attached=1;return 0;
 case PTRACE_INTERRUPT:stopped_state=1;waiting=1;step_event=0;return 0;
 case PTRACE_GETHBPREGS:{unsigned k=(unsigned)(uintptr_t)addr;*(uint32_t*)data=k?bp[k-1]:(0x06080400u|(unsigned)resources);return 0;}
 case PTRACE_SETHBPREGS:{unsigned k=(unsigned)(uintptr_t)addr;uint32_t v=*(uint32_t*)data;if(!k||k>10||((k&1u)==0u&&v!=0x1e5u&&v!=0x1e4u)){errno=EINVAL;return-1;}writes++;if(writes==fail_write){errno=EIO;return-1;}bp[k-1]=v;return 0;}
 case PTRACE_CONT:stopped_state=0;waiting=2;step_event=0;return 0;
 case PTRACE_SINGLESTEP:stopped_state=0;waiting=3;step_event=1;return 0;
 case PTRACE_GETREGS:{uint32_t*r=data;memset(r,0,72);r[4]=event_index<3?0x10010000:0x11020000;r[6]=0x11010000;r[15]=current_pc;r[16]=0x10;return 0;}
 case PTRACE_GETSIGINFO:{siginfo_t*i=data;memset(i,0,sizeof*i);i->si_code=step_event?1:4;i->si_addr=(void*)(uintptr_t)(step_event?current_pc:points[event_index]);return 0;}
 case PTRACE_DETACH:detach_signal=(int)(uintptr_t)data;attached=stopped_state=0;memset(bp,0,sizeof bp);return 0;
 default:fprintf(stderr,"UNEXPECTED PTRACE %d\n",request);abort();
 }
}
static pid_t model_waitpid(pid_t tid,int*status,int options){
 if(exit_wait){exit_wait=0;*status=0;attached=0;memset(bp,0,sizeof bp);return tid;}
 (void)options;if(waiting==1){*status=(PTRACE_EVENT_STOP<<16)|(SIGTRAP<<8)|0x7f;waiting=0;stopped_state=1;return tid;}
 if(waiting==2){if(event_index>=5){wall+=4000000000u;errno=EINTR;return-1;}current_pc=points[event_index]+(wrong_pc?4u:0u);*status=(SIGTRAP<<8)|0x7f;waiting=0;stopped_state=1;return tid;}
 if(waiting==3){current_pc+=4;wall+=large_stop?10000000u:1000u;*status=(SIGTRAP<<8)|0x7f;waiting=0;stopped_state=1;return tid;}
 errno=EINTR;return-1;
}
/* Advance the modeled target only when the tracer resumes after a step. */
static long advancing_ptrace(int request,...){
 va_list ap;va_start(ap,request);int tid=va_arg(ap,int);void*a=va_arg(ap,void*);void*d=va_arg(ap,void*);va_end(ap);
 if(request==PTRACE_CONT&&step_event)event_index++;
 return model_ptrace(request,tid,a,d);
}
#define ptrace advancing_ptrace
#define pread model_pread
#define waitpid model_waitpid
#define opendir model_opendir
#define readdir model_readdir
#define closedir model_closedir
#include "../src/hevc_stage_trace.inc"
#undef ptrace
static void reset(struct ht_worker*c,struct ht_header*h,struct ht_record*r){
 memset(c,0,sizeof*c);memset(h,0,sizeof*h);memset(r,0,64*16);memset(bp,0,sizeof bp);task_count=1;foreign_tracer=0;resources=5;task_state='S';camera_ticks=1;attached=stopped_state=waiting=step_event=event_index=detach_signal=writes=fail_write=large_stop=wrong_pc=exit_wait=0;interrupted=0;c->pid=247;c->camera_start=1;c->mem=7;c->bases[0]=0x10000000;c->bases[1]=0x11000000;c->h=h;c->records=r;h->w[3]=HT_CAPACITY;
}
int main(void){
 struct ht_worker c;struct ht_header h;struct ht_record r[16];
 reset(&c,&h,r);CHECK(ht_arm(&c));CHECK(h.w[9]==5);CHECK(ht_window(&c,wall+3000000000u));CHECK(ht_detach(&c));CHECK(!attached&&h.w[12]&&h.w[4]==5);for(unsigned i=0;i<5;i++){CHECK(r[i].kind==i+1&&r[i].request==1);CHECK(r[i].y==0x9a000000&&r[i].uv==0x9a7e9000);CHECK(r[i].commit==(vp_hash(r+i,60)^HT_XOR));}
 h.w[0]=HT_MAGIC;h.w[1]=1;h.w[2]=64;h.w[5]=3;h.w[10]=1;h.w[31]=vp_hash(&h,124);
 printf("FIXTURE_HEVC ");for(unsigned i=0;i<sizeof h;i++)printf("%02x",((const unsigned char*)&h)[i]);for(unsigned i=0;i<5*64;i++)printf("%02x",((const unsigned char*)r)[i]);puts("");
 puts("PASS actual ARM HEVC native boundaries, buffer release and kernel ARM step protocol");
 reset(&c,&h,r);resources=4;CHECK(!ht_arm(&c));CHECK(ht_detach(&c));CHECK(!writes&&h.w[12]);
 reset(&c,&h,r);task_count=2;CHECK(!ht_arm(&c));CHECK(!attached&&!writes);
 reset(&c,&h,r);foreign_tracer=123;CHECK(!ht_arm(&c));CHECK(!attached&&!writes);
 puts("PASS insufficient resources, duplicate encoder and foreign tracer refused");
 for(int failure=1;failure<=10;failure++){reset(&c,&h,r);fail_write=failure;CHECK(!ht_arm(&c));fail_write=0;CHECK(ht_detach(&c));CHECK(!attached&&h.w[12]);}
 puts("PASS every setup write failure restores/disables owned breakpoints and detaches");
 reset(&c,&h,r);CHECK(ht_arm(&c));large_stop=1;CHECK(!ht_window(&c,wall+3000000000u));CHECK(h.w[6]&32u);CHECK(ht_detach(&c));CHECK(h.w[12]);
 reset(&c,&h,r);c.current.object=0x10010000;c.current.y=0x9a000000;c.current.uv=0x9a7e9000;c.current.request=99;CHECK(ht_arm(&c));CHECK(!c.current.request);CHECK(ht_detach(&c));
 puts("PASS long observer stop aborts; old-window request IDs never leak into a new window");
 reset(&c,&h,r);CHECK(ht_arm(&c));wrong_pc=1;CHECK(!ht_window(&c,wall+3000000000u));CHECK(ht_detach(&c));CHECK(!detach_signal);
 reset(&c,&h,r);CHECK(ht_arm(&c));exit_wait=1;CHECK(ht_window(&c,wall+3000000000u));CHECK(ht_detach(&c));CHECK(h.w[12]);
 reset(&c,&h,r);task_state='T';CHECK(!ht_arm(&c));CHECK(!attached);
 reset(&c,&h,r);camera_ticks=2;CHECK(!ht_arm(&c));CHECK(!attached);
 puts("PASS own unexpected-PC trap never reaches app; target exit, stopped target and PID reuse handled");
 return 0;
}
