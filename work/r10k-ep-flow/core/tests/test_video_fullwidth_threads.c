#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
static unsigned seized,detached,interrupts,fail_seize,fail_interrupt,fail_detach;
static long modeled_ptrace(int req,...){va_list a;va_start(a,req);int t=va_arg(a,int);(void)va_arg(a,void*);(void)va_arg(a,uintptr_t);va_end(a);assert(t>=123&&t<=130&&t!=124);switch(req){case PTRACE_SEIZE:if(++seized==fail_seize){errno=EPERM;return -1;}return 0;case PTRACE_INTERRUPT:if(++interrupts==fail_interrupt){errno=EIO;return -1;}return 0;case PTRACE_DETACH:if(fail_detach){errno=ESRCH;return -1;}detached++;return 0;default:assert(0);return -1;}}
static pid_t modeled_wait(pid_t p,int*s,int options){assert(options==(__WALL|WNOHANG));*s=(PTRACE_EVENT_STOP<<16)|(SIGTRAP<<8)|127;return p;}
static ssize_t modeled_link(const char*p,char*b,size_t n){(void)p;const char*s="/mnt/mmc/DCIM/305PHOTO/SAM_0383.MP4";assert(n>strlen(s));memcpy(b,s,strlen(s));return(ssize_t)strlen(s);}
#define ptrace modeled_ptrace
#define waitpid modeled_wait
#define readlink modeled_link
#define main video_fullwidth_unused_main
#include "../src/nxl-video-fullwidth.c"
#undef main
static void flags(const char*s){FILE*f=fopen("/proc/123/fdinfo/7","w");assert(f&&fputs(s,f)>=0&&!fclose(f));}
static void reset(void){memset(held,0,sizeof held);held_count=0;seized=detached=interrupts=fail_seize=fail_interrupt=fail_detach=0;ui_thawed=1;stop_requested=0;}
int main(void){pid=123;tid=124;reset();assert(freeze_ui()&&held_count==7&&!ui_thawed&&seized==7);assert(thaw_ui()&&ui_thawed&&detached==7);
 reset();fail_seize=3;assert(!freeze_ui());assert(thaw_ui()&&detached==2);reset();fail_interrupt=2;assert(!freeze_ui());assert(thaw_ui()&&detached==2);reset();assert(freeze_ui());fail_detach=1;assert(!thaw_ui()&&!ui_thawed);fail_detach=0;assert(thaw_ui()&&detached==7);
 reset();assert(freeze_ui());stop_requested=1;assert(thaw_ui()&&stop_requested==1);stop_requested=0;
 flags("flags:\t0100000\n");assert(busy()==0);flags("flags:\t0100001\n");assert(busy()==1);flags("flags:\t0100002\n");assert(busy()==1);flags("pos:\t0\n");assert(busy()==-1);
 puts("PASS actual full-width helper UI freeze/thaw, excluded native worker, partial attach/interrupt cleanup, live ESRCH retry, signal cleanup and recording guards; device calls modeled.");return 0;}
