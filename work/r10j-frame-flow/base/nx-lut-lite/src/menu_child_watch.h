/* Child lifetime is independent of its stdout. Wake the stage loop on exit
 * without a repaint timer or an early blocking waitpid after pipe EOF. */
#ifndef NXL_MENU_CHILD_WATCH_H
#define NXL_MENU_CHILD_WATCH_H
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
static int task_child_pipe[2]={-1,-1},task_child_watch_active;
static volatile sig_atomic_t task_child_signal_fd=-1;
static struct sigaction task_child_previous;
static sigset_t task_child_previous_mask;
static int task_child_mask_saved;
static void task_child_signal(int sig){
 (void)sig;int saved=errno;unsigned char wake=1;int fd=(int)task_child_signal_fd;
 if(fd>=0)(void)write(fd,&wake,1);errno=saved;
}
static void task_child_watch_close(void){
 if(task_child_watch_active)sigaction(SIGCHLD,&task_child_previous,NULL);
 task_child_watch_active=0;task_child_signal_fd=-1;
 for(unsigned i=0;i<2;i++){if(task_child_pipe[i]>=0)close(task_child_pipe[i]);task_child_pipe[i]=-1;}
 if(task_child_mask_saved)sigprocmask(SIG_SETMASK,&task_child_previous_mask,NULL);
 task_child_mask_saved=0;
}
static int task_child_watch_open(void){
 if(task_child_watch_active||pipe(task_child_pipe))return 0;
 for(unsigned i=0;i<2;i++){
  int fd=task_child_pipe[i],flags=fcntl(fd,F_GETFL),cloexec=fcntl(fd,F_GETFD);
  if(flags<0||cloexec<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,cloexec|FD_CLOEXEC)){task_child_watch_close();return 0;}
 }
 struct sigaction action;memset(&action,0,sizeof action);action.sa_handler=task_child_signal;sigemptyset(&action.sa_mask);action.sa_flags=SA_NOCLDSTOP;
 task_child_signal_fd=task_child_pipe[1];
 if(sigaction(SIGCHLD,&action,&task_child_previous)){task_child_watch_close();return 0;}
 task_child_watch_active=1;
 sigset_t child;sigemptyset(&child);sigaddset(&child,SIGCHLD);
 if(sigprocmask(SIG_UNBLOCK,&child,&task_child_previous_mask)){task_child_watch_close();return 0;}
 task_child_mask_saved=1;return 1;
}
static void task_child_watch_drain(void){
 /* One wake is sufficient; the following loop always checks this exact PID. */
 unsigned char bytes[64];(void)read(task_child_pipe[0],bytes,sizeof bytes);
}
static void task_child_watch_detach(void){task_child_watch_close();}
#endif
