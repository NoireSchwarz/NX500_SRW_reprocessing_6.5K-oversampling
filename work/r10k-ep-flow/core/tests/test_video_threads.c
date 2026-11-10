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
static unsigned attached_count,detached_count,reg_count,pc_in_window,fail_detach;
static long modeled_ptrace(int req,...){va_list a;va_start(a,req);int tid=va_arg(a,int);(void)va_arg(a,void*);uintptr_t data=va_arg(a,uintptr_t);va_end(a);assert(tid==123||tid==124);switch(req){case PTRACE_SEIZE:attached_count++;return 0;case PTRACE_INTERRUPT:return 0;case PTRACE_GETREGS:reg_count++;((uint32_t*)data)[15]=pc_in_window?0xe4428:0x120000;return 0;case PTRACE_DETACH:if(fail_detach){errno=ESRCH;return -1;}detached_count++;return 0;default:assert(0);return -1;}}
static pid_t modeled_wait(pid_t pid,int*status,int options){assert(options==(__WALL|WNOHANG));*status=(PTRACE_EVENT_STOP<<16)|(SIGTRAP<<8)|127;return pid;}
static ssize_t modeled_link(const char*p,char*b,size_t n){(void)p;const char*s="/mnt/mmc/DCIM/305PHOTO/SAM_0383.MP4";assert(n>strlen(s));memcpy(b,s,strlen(s));return(ssize_t)strlen(s);}
#define ptrace modeled_ptrace
#define waitpid modeled_wait
#define readlink modeled_link
#define main video_helper_unused_main
#include "../src/nxl-video-mode.c"
#undef main
static void flags(const char*s){FILE*f=fopen("/proc/123/fdinfo/7","w");assert(f&&fputs(s,f)>=0&&!fclose(f));}
int main(void){
 struct camera c={.pid=123,.mem=-1};assert(freeze(&c)&&c.count==2&&attached_count==2&&reg_count==2);assert(thaw(&c)&&detached_count==2);
 c=(struct camera){.pid=123,.mem=-1};pc_in_window=1;assert(!freeze(&c));assert(thaw(&c)&&attached_count==detached_count);pc_in_window=0;
 c=(struct camera){.pid=123,.mem=-1};assert(freeze(&c));cancelled=1;assert(thaw(&c)&&cancelled==1);cancelled=0;
 c=(struct camera){.pid=123,.mem=-1};assert(freeze(&c));fail_detach=1;assert(!thaw(&c));assert(c.threads[0].attached&&c.threads[1].attached);fail_detach=0;assert(thaw(&c)&&attached_count==detached_count);
 flags("pos:\t0\nflags:\t0100000\n");assert(busy(&c)==0);flags("pos:\t0\nflags:\t0100001\n");assert(busy(&c)==1);flags("pos:\t0\nflags:\t0100002\n");assert(busy(&c)==1);flags("pos:\t0\n");assert(busy(&c)==-1);
 printf("PASS %u modeled thread attachments/detachments, constructor-PC rejection, signal cleanup, alive-ESRCH retry and writable MP4/fdinfo guards; no ptrace syscall executed\n",attached_count);return 0;
}
