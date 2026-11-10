/* Display facts in volatile RAM. No hardware authority or image I/O.
 * No new libc stat/clock imports in the original-ABI native worker. */
#ifndef NXL_PROGRESS_EMIT_H
#define NXL_PROGRESS_EMIT_H
#include <sys/syscall.h>
#include "../core/src/sha256.h"
#include "progress_journal.h"
static void nxl_live_progress(const char*phase,const char*state,const char*detail){
 const char*operation=getenv("NX51_PROGRESS_TOKEN");if(!np_operation(operation)||!np_text(phase,49,1)||!np_text(state,49,1)||!np_text(detail,1025,0))return;
 /* ARM syscall263 uses two 32-bit words even with a time64 musl sysroot. */
 uint32_t now[2];
#if defined(__arm__)
 if(syscall(263,1,now))return;
#else
 struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return;now[0]=(uint32_t)t.tv_sec;now[1]=(uint32_t)t.tv_nsec;
#endif
 struct np_record r={0};r.magic=NP_MAGIC;r.version=3;r.bytes=sizeof r;r.sequence=(now[0]^now[1])|1u;r.pid=(uint32_t)getpid();r.mono_sec=now[0];r.mono_nsec=now[1];
 strcpy(r.phase,phase);strcpy(r.state,state);strcpy(r.detail,detail);strcpy(r.stamp,"UNAVAILABLE");strcpy(r.operation,operation);np_digest(&r,r.digest);
 np_stage_send(&r);
 int fd=open("/tmp/nx-lut-lite/PROGRESS.live",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);if(fd<0)return;
 if(!flock(fd,LOCK_EX|LOCK_NB)){ssize_t written=write(fd,&r,sizeof r);if(written==sizeof r)(void)fsync(fd);}
 close(fd);
}
#endif
