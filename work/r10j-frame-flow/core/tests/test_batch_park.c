/* Actual Linux child SIGSTOP test; sandbox forbids PTRACE_ATTACH.
 * Detach instruction/ABI remains static-review scope, not executed here. */
#define _GNU_SOURCE
#define P48_PARK_STOPPED test_child_stopped
static int test_child_stopped(void);
#define main production_main_not_run
#include "nxl-native-front.c"
#undef main
#include <assert.h>
#include <pthread.h>
static int child_was_stopped;
static int test_child_stopped(void){int s=0;pid_t got=waitpid(pid,&s,WUNTRACED|WNOHANG);if(got==pid&&WIFSTOPPED(s))child_was_stopped=1;return child_was_stopped;}
static void *sleeper(void *p){(void)p;for(;;)pause();return NULL;}
int main(int argc,char **argv){
 assert(argc==2);batch_directory=argv[1];char path[1200];snprintf(path,sizeof path,"%s/park.log",argv[1]);log_file=fopen(path,"w");assert(log_file);trace_file=log_file;started=ns();
 int channel[2];assert(!pipe(channel));pid=fork();assert(pid>=0);
 if(!pid){close(channel[0]);pthread_t threads[6];for(unsigned i=0;i<6;i++)assert(!pthread_create(threads+i,NULL,sleeper,NULL));for(;;){if(write(channel[1],"R",1)!=1)_exit(0);usleep(10000);}}
 close(channel[1]);int flags=fcntl(channel[0],F_GETFL);assert(!fcntl(channel[0],F_SETFL,flags|O_NONBLOCK));tid=pid;
 char ready;for(unsigned n=0;n<100&&read(channel[0],&ready,1)!=1;n++)usleep(10000);
 attached=trace_stopped=0;p48_unverified_shell_message=1;
 assert(p48_park_uncertain_command());assert(p48_parked_camera&&camera_paused&&!attached);assert(resume_camera());assert(camera_paused&&test_child_stopped());
 char b[256];while(read(channel[0],b,sizeof b)>0){}usleep(60000);assert(read(channel[0],b,sizeof b)<0&&errno==EAGAIN);assert(test_child_stopped());
 /* Cleanup belongs only to this test, never the production parked path. */
 assert(!kill(pid,SIGCONT));usleep(30000);assert(read(channel[0],b,sizeof b)>0);assert(!kill(pid,SIGKILL));int st;assert(waitpid(pid,&st,0)==pid);close(channel[0]);assert(!fclose(log_file));puts("PASS production fail-closed SIGSTOP retains actual child stop; resume_camera refuses auto-resume; ptrace-detach NOT exercised (sandbox restriction); NO CAMERA");return 0;
}
