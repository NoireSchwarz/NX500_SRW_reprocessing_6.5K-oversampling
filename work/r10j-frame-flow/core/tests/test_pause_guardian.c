#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include "pause_guardian.h"
int main(void){
 for(unsigned mode=0;mode<3;mode++){
  int ready[2],control[2];assert(!pipe(ready)&&!pipe(control));pid_t camera=fork();assert(camera>=0);
  if(!camera){close(ready[0]);close(control[0]);close(control[1]);raise(SIGSTOP);assert(write(ready[1],"R",1)==1);_exit(0);}
  close(ready[1]);int state;assert(waitpid(camera,&state,WUNTRACED)==camera&&WIFSTOPPED(state));pid_t guardian=fork();assert(guardian>=0);
  if(!guardian){close(control[1]);close(ready[0]);_exit(p39_pause_guardian(control[0],camera,mode==2?50:2000));}
  close(control[0]);if(mode==0){assert(write(control[1],"D",1)==1);close(control[1]);assert(waitpid(guardian,&state,0)==guardian&&WIFEXITED(state)&&WEXITSTATUS(state)==0);assert(waitpid(camera,&state,WNOHANG|WCONTINUED)==0);assert(!kill(camera,SIGCONT));}
  else{if(mode==1)close(control[1]);assert(waitpid(guardian,&state,0)==guardian&&WIFEXITED(state)&&WEXITSTATUS(state)==1);if(mode==2)close(control[1]);}
  char c;assert(read(ready[0],&c,1)==1&&c=='R');close(ready[0]);assert(waitpid(camera,&state,0)==camera&&WIFEXITED(state)&&WEXITSTATUS(state)==0);
 }
 puts("PASS 3 actual stopped-child guardian normal/pipe-loss/timeout recovery cases; NO CAMERA");return 0;
}
