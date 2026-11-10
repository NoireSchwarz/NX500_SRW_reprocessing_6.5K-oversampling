#define main rearm_unused_main
#include "../src/nxl-session-rearm.c"
#undef main
#include <assert.h>
static uint32_t test_pc=0x180c,test_sp=0x8000,test_delta=0x120;static unsigned mode,now;
static int tq(void*x,int*q){(void)x;*q=123;return 1;}
static int tw(void*x,unsigned*n,int*t){(void)x;*n=1;*t=777;return 1;}
static int tl(void*x,int tid,const char*leaf,char*b,size_t n){(void)x;(void)tid;if(!strcmp(leaf,"wchan")){snprintf(b,n,"%s",mode==2?"futex_wait_queue_me":"do_msgrcv");return 1;}snprintf(b,n,"0 0x%x 0x%x 0xd0 0x0 0x0 0x0 0x%x 0x%x",mode==1?124:123,test_sp+test_delta,test_sp,test_pc);return 1;}
static uint64_t tm(void*x){(void)x;return now;}
static int tp(void*x,unsigned ms){(void)x;now+=ms;return 1;}
int main(void){
 camera_pid=777;procfd=open("/proc/777/mem",O_RDONLY);assert(procfd>=0);libc_base=0x1000;shell_base=0x5000;
 struct shell_idle_ops ops={tq,tw,tl,NULL,NULL,tm,tp,shell_receive_context};struct shell_idle_report r;
 assert(shell_idle_wait(NULL,&ops,libc_base,shell_base,&r)&&r.parked);
 test_pc=0x2404;test_delta=0x220;now=0;assert(shell_idle_wait(NULL,&ops,libc_base,shell_base,&r));
 mode=1;now=0;assert(!shell_idle_wait(NULL,&ops,libc_base,shell_base,&r));mode=2;now=0;assert(!shell_idle_wait(NULL,&ops,libc_base,shell_base,&r));
 mode=0;test_pc=0x2604;now=0;assert(!shell_idle_wait(NULL,&ops,libc_base,shell_base,&r));test_pc=0x2404;test_delta=0x2000;now=0;assert(!shell_idle_wait(NULL,&ops,libc_base,shell_base,&r));
 puts("PASS diagnostic syscall/wchan/mapping/SVC proof tolerates different PC and stack frame; wrong queue, blocker, instruction and buffer rejected; native injector unchanged");return 0;
}
