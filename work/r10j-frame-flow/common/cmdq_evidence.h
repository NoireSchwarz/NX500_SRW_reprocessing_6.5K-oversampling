/* Optional RAM-only evidence of the original CMDQ/DVFS roots. No authority,
 * MMIO access, semaphore operation, reset, IRQ/clock write or retry. */
#ifndef NXL_CMDQ_EVIDENCE_H
#define NXL_CMDQ_EVIDENCE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include "physical_ro.h"
#define CQ_EVIDENCE_MAGIC 0x31514443u
struct cq_evidence {
 uint32_t magic,version,bytes,pid,read_mask,mono_sec,mono_nsec,clock_valid;
 char phase[40];
 uint32_t address[5],words[5],data[2112],checksum;
};
static uint32_t cq_checksum(const void*p,unsigned n){uint32_t h=2166136261u;const unsigned char*b=p;while(n--)h=(h^*b++)*16777619u;return h;}
static int cq_phase(const char*p){
 const char*phases[]={"BEFORE_SRW","AFTER_STILL","BEFORE_EP","AFTER_EP","AFTER_RELEASE_CALL","AFTER_RELEASE_CHECK","AFTER_RELEASE","BEFORE_RESTORE","AFTER_RESTORE"};
 for(unsigned i=0;i<sizeof phases/sizeof *phases;i++)if(!strcmp(p,phases[i]))return 1;return 0;
}
static int cq_capture(const char *directory,const char *phase){
 int saved=errno;if(!cq_phase(phase)){errno=saved;return 0;}
 struct cq_evidence r={0};r.magic=CQ_EVIDENCE_MAGIC;r.version=1;r.bytes=sizeof r;r.pid=(uint32_t)getpid();strcpy(r.phase,phase);
 uint32_t stamp[2];if(!syscall(263,1,stamp)){r.mono_sec=stamp[0];r.mono_nsec=stamp[1];r.clock_valid=1;}
 /* Globals referenced by original 80043268..80043350 enqueue/wait and
  * its diagnostic counters. The fifth range is the guarded semaphore object
  * referenced by the captured root, if it lies in ordinary RTOS RAM. */
 r.address[0]=0x813f0000u;r.words[0]=1024;r.address[1]=0x813f1000u;r.words[1]=1024;
 r.address[2]=0x817e0680u;r.words[2]=16;r.address[3]=0x81000080u;r.words[3]=1;
 struct pr_reader reader={.fd=-1};reader.fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);
 unsigned at=0;
 if(reader.fd>=0){
  for(unsigned i=0;i<4;i++){if(pr_read(&reader,r.address[i],r.data+at,r.words[i]*4))r.read_mask|=1u<<i;at+=r.words[i];}
  uint32_t sem=r.data[1024+1024+6];
  if((r.read_mask&4)&&sem>=0x81000000u&&sem<=0x93ffffc0u&&!(sem&3u)&&((sem&4095u)+64u<=4096u)){
   r.address[4]=sem;r.words[4]=16;if(pr_read(&reader,sem,r.data+at,64))r.read_mask|=16;
  }
  pr_close(&reader);close(reader.fd);
 }
 r.checksum=cq_checksum(&r,offsetof(struct cq_evidence,checksum));char path[1400];
 int length=snprintf(path,sizeof path,"%s/diag-cmdq-%s.bin",directory,phase),ok=0;
 if(length>0&&(unsigned)length<sizeof path){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd>=0){ok=write(fd,&r,sizeof r)==sizeof r&&!fsync(fd);if(close(fd))ok=0;}}
 errno=saved;return ok;
}
#endif
