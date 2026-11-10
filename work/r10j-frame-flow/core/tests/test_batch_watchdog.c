/* Actual fork/pipe/signal exercise of H/V/P watchdog modes. No ptrace/camera. */
#define _GNU_SOURCE
#define main production_main_not_run
#include "nxl-native-front.c"
#undef main
#include <assert.h>
int main(int argc,char **argv){
 assert(argc==2);for(unsigned mode=0;mode<3;mode++){
  int ready[2];assert(!pipe(ready));pid_t camera=fork();assert(camera>=0);
  if(!camera){close(ready[0]);assert(write(ready[1],"R",1)==1);for(;;)pause();}
  close(ready[1]);char c;assert(read(ready[0],&c,1)==1);close(ready[0]);pid_t worker=fork();assert(worker>=0);
  if(!worker){pid=camera;char path[1200];snprintf(path,sizeof path,"%s/watchdog-%u.log",argv[1],mode);log_file=fopen(path,"w");if(!log_file)_exit(20);started=ns();if(!watchdog_start()||!watchdog_mode('H'))_exit(21);if(mode==1&&!watchdog_mode('V'))_exit(22);if(mode==2&&!watchdog_mode('P'))_exit(23);_exit(0);}
  int st;assert(waitpid(worker,&st,0)==worker&&WIFEXITED(st)&&WEXITSTATUS(st)==0);usleep(200000);
  pid_t got=waitpid(camera,&st,WUNTRACED|WNOHANG);
  if(mode==0)assert(got==camera&&WIFSTOPPED(st)&&WSTOPSIG(st)==SIGSTOP);else assert(got==0);
  assert(!kill(camera,SIGCONT));assert(!kill(camera,SIGKILL));assert(waitpid(camera,&st,0)==camera);
 }
 puts("PASS actual watchdog H+parent-death parks child, H/V clears hold, H/P exits without restore; NO CAMERA/NO PTRACE");return 0;
}
