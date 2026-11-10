/* SPDX-License-Identifier: AGPL-3.0-only. Resume only this saved camera PID on
 * parent pipe loss or timeout. Shared by production and actual child tests. */
#ifndef P39_PAUSE_GUARDIAN_H
#define P39_PAUSE_GUARDIAN_H
static int p39_pause_guardian(int read_fd,int camera_pid,unsigned timeout_ms){
 struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
 uint64_t end=(uint64_t)now.tv_sec*1000u+(unsigned)(now.tv_nsec/1000000u)+timeout_ms;
 unsigned inflight=0;
 for(;;){
  clock_gettime(CLOCK_MONOTONIC,&now);uint64_t at=(uint64_t)now.tv_sec*1000u+(unsigned)(now.tv_nsec/1000000u);
  if(at>=end)break;struct pollfd f={read_fd,POLLIN|POLLHUP,0};int rc=poll(&f,1,(int)(end-at));
  if(rc<0&&errno==EINTR)continue;
  char c=0;if(rc>0&&(f.revents&POLLIN)&&read(read_fd,&c,1)==1){
   if(c=='I'){inflight=1;continue;}
   if(c=='D'||c=='P'){close(read_fd);return 0;}
  }
  break;
 }
 /* After GO, EOF/timeout is not proof that DMA ended. Keep this exact PID
  * stopped until a cold restart; CAP and callbacks remain allocated. */
 if(!inflight)kill(camera_pid,SIGCONT);close(read_fd);return inflight?2:1;
}
#endif
