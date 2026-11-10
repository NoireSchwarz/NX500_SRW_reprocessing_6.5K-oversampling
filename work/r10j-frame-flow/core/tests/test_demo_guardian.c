/* SPDX-License-Identifier: AGPL-3.0-only.
 * Actual fork/pipe/poll/signals test of the production pause_guardian.h.
 * Every SIGSTOP/SIGCONT target is a child forked by this test. No device,
 * camera PID discovery, ptrace, /dev/mem or firmware operation is present.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "pause_guardian.h"
static pid_t owned[64];static unsigned owned_n;
static uint64_t millis(void){struct timespec x;if(clock_gettime(CLOCK_MONOTONIC,&x))_exit(120);return (uint64_t)x.tv_sec*1000u+(unsigned)(x.tv_nsec/1000000u);}
static void track(pid_t p){if(p<=1||owned_n>=64)_exit(121);owned[owned_n++]=p;}
static void untrack(pid_t p){for(unsigned i=0;i<owned_n;i++)if(owned[i]==p)owned[i]=0;}
static void cleanup(void){for(unsigned i=0;i<owned_n;i++)if(owned[i]>1){kill(owned[i],SIGCONT);kill(owned[i],SIGTERM);while(waitpid(owned[i],NULL,0)<0&&errno==EINTR){}owned[i]=0;}}
static void timedout(int sig){(void)sig;for(unsigned i=0;i<owned_n;i++)if(owned[i]>1){kill(owned[i],SIGCONT);kill(owned[i],SIGTERM);}_exit(124);}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL line=%d expr=%s errno=%d\n",__LINE__,#x,errno);exit(1);}}while(0)
static int wait_done(pid_t child,int *status,unsigned timeout){uint64_t end=millis()+timeout;for(;;){pid_t p=waitpid(child,status,WNOHANG);if(p==child){untrack(child);return 1;}if(p<0&&errno!=EINTR)return 0;if(millis()>=end)return 0;struct timespec d={0,1000000};nanosleep(&d,NULL);}}
static int readable(int fd,unsigned timeout){struct pollfd p={fd,POLLIN|POLLHUP,0};int n;do{n=poll(&p,1,(int)timeout);}while(n<0&&errno==EINTR);CHECK(n>=0);return n>0&&(p.revents&POLLIN);}
static pid_t stopped_child(int *read_fd){int p[2];CHECK(pipe(p)==0);pid_t child=fork();CHECK(child>=0);if(child==0){close(p[0]);raise(SIGSTOP);char c='R';if(write(p[1],&c,1)!=1)_exit(90);close(p[1]);_exit(0);}track(child);close(p[1]);int st;CHECK(waitpid(child,&st,WUNTRACED)==child&&WIFSTOPPED(st)&&WSTOPSIG(st)==SIGSTOP);*read_fd=p[0];return child;}
static void finish_target(pid_t child,int fd,int expected_resume){
 if(!expected_resume){CHECK(!readable(fd,70));int st=0;CHECK(waitpid(child,&st,WNOHANG|WCONTINUED)==0);CHECK(kill(child,SIGCONT)==0);}
 CHECK(readable(fd,1000));char c=0;CHECK(read(fd,&c,1)==1&&c=='R');close(fd);int st=0;CHECK(wait_done(child,&st,1000)&&WIFEXITED(st)&&WEXITSTATUS(st)==0);
}
struct scenario {const char *name,*message;int timeout,expected,resume;};
int main(void){
 CHECK(atexit(cleanup)==0);signal(SIGALRM,timedout);signal(SIGPIPE,SIG_IGN);alarm(20);
 const struct scenario cases[]={
  {"before_GO_EOF","",0,1,1},
  {"before_GO_timeout","",1,1,1},
  {"D_before_GO_no_auto_CONT","D",0,0,0},
  {"P_before_GO_no_auto_CONT","P",0,0,0},
  {"I_then_EOF_hold","I",0,2,0},
  {"I_then_timeout_hold","I",1,2,0},
  {"I_then_D_no_auto_CONT","ID",0,0,0},
  {"I_then_P_no_auto_CONT","IP",0,0,0}
 };
 puts("{\"format\":\"P49_REAL_HOST_GUARDIAN_TEST1\",\"scope\":\"owned forked children only; no camera/device/ptrace\",\"cases\":[");
 for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++){
  int target_fd,bystander_fd;pid_t target=stopped_child(&target_fd),bystander=stopped_child(&bystander_fd);
  int channel[2];CHECK(pipe(channel)==0);pid_t guard=fork();CHECK(guard>=0);
  if(!guard){close(channel[1]);close(target_fd);close(bystander_fd);signal(SIGALRM,SIG_DFL);_exit(p39_pause_guardian(channel[0],target,cases[i].timeout?80:1000));}
  track(guard);close(channel[0]);size_t count=strlen(cases[i].message);if(count)CHECK(write(channel[1],cases[i].message,count)==(ssize_t)count);
  if(!cases[i].timeout){close(channel[1]);channel[1]=-1;}
  int st=0;CHECK(wait_done(guard,&st,2000)&&WIFEXITED(st)&&WEXITSTATUS(st)==cases[i].expected);
  if(channel[1]>=0)close(channel[1]);
  /* A stopped, unrelated OWNED child proves the exact saved PID is the only
   * signal target; it never serves as a substitute or process-name lookup. */
  CHECK(!readable(bystander_fd,40));int bstate=0;CHECK(waitpid(bystander,&bstate,WNOHANG|WCONTINUED)==0);
  finish_target(target,target_fd,cases[i].resume);finish_target(bystander,bystander_fd,0);
  printf("%s{\"name\":\"%s\",\"guardian_exit\":%d,\"target_resumed_by_guardian\":%s,\"bystander_stayed_stopped\":true,\"passed\":true}\n",i?",":"",cases[i].name,cases[i].expected,cases[i].resume?"true":"false");
 }
 puts("],\"passed\":8,\"failed\":0}");alarm(0);return 0;
}
