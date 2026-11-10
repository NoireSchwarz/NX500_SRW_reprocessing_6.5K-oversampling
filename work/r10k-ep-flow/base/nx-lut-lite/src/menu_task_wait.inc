/* Keep receiving stages through stdout EOF. Only waitpid for this child can
 * end the task. SIGCHLD/self-pipe prevents the exit-before-poll race. */
static pid_t task_wait_backend(pid_t worker,int fd,FILE *log,int *status){
 int flags=fcntl(fd,F_GETFL),nonblock=flags>=0?fcntl(fd,F_SETFL,flags|O_NONBLOCK):-1;
 task_trace_event(nonblock<0?"OUTPUT_NONBLOCK_FAILED":"OUTPUT_NONBLOCK_READY","backend output",flags);
 pid_t done=0;int fallback=0;
 for(;;){
  task_progress_pump();
  do{done=waitpid(worker,status,WNOHANG);}while(done<0&&errno==EINTR);
  if(done==worker||done<0)break;
  struct pollfd io[3]={{task_phase_fd,POLLIN,0},{fd,POLLIN|POLLHUP,0},{task_child_pipe[0],POLLIN,0}};
  int ready=poll(io,3,fallback?200:-1);
  if(ready<0){if(errno==EINTR)continue;task_trace_event("TASK_POLL_FAILED","continuing child supervision",errno);fallback=1;continue;}
  if(io[2].revents&POLLIN)task_child_watch_drain();
  if(io[0].revents&POLLIN)task_progress_pump();
  if(fd>=0&&(io[1].revents&(POLLIN|POLLHUP))){
   char bytes[256];ssize_t n=read(fd,bytes,sizeof bytes);
   if(n>0){if(log)fwrite(bytes,1,(size_t)n,log);}
   else if(!n){task_trace_event("OUTPUT_EOF_CHILD_STILL_SUPERVISED","stage channel remains active",(int)worker);close(fd);fd=-1;}
   else if(errno!=EINTR&&errno!=EAGAIN&&errno!=EWOULDBLOCK){task_trace_event("OUTPUT_READ_FAILED","stage channel remains active",errno);close(fd);fd=-1;}
  }
  if(fd>=0&&(io[1].revents&(POLLERR|POLLNVAL))){close(fd);fd=-1;}
 }
 /* Drain only data already readable after exit, bounded even if another
  * descendant kept stdout open. This never waits for that descendant. */
 if(fd>=0){for(unsigned i=0;i<128;i++){struct pollfd p={fd,POLLIN,0};if(poll(&p,1,0)<=0||!(p.revents&POLLIN))break;char b[256];ssize_t n=read(fd,b,sizeof b);if(n<=0)break;if(log)fwrite(b,1,(size_t)n,log);}close(fd);}
 task_progress_pump();task_child_watch_close();
 task_trace_event("CHILD_REAPED",done==worker?"verified child exit":"wait failed",done==worker?*status:errno);return done;
}
