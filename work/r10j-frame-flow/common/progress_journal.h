/* SPDX-License-Identifier: AGPL-3.0-only. Human display, no hardware authority.
 * Fixed checksummed append records avoid replacement of an SD directory leaf.
 * Readers ignore a torn tail and show only the last complete valid record. */
#ifndef NX_PROGRESS_JOURNAL_H
#define NX_PROGRESS_JOURNAL_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <time.h>
#ifndef NP_FSTAT
#define NP_FSTAT fstat
#endif
#define NP_MAGIC 0x3250474eu
struct np_record {uint32_t magic,version,bytes,sequence,pid,mono_sec,mono_nsec;char phase[52],state[52],detail[1032],stamp[68],operation[36];unsigned char reserved[748],digest[32];};
_Static_assert(sizeof(struct np_record)==2048,"progress record ABI");
static int np_text(const char*s,unsigned cap,int id){unsigned n=0;for(;n<cap&&s[n];n++){unsigned char c=s[n];if(c<32||c==127||(id&&!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-')))return 0;}return n>0&&n<cap;}
static void np_digest(const struct np_record*r,unsigned char out[32]){struct sha256 h;sha256_init(&h);sha256_update(&h,r,offsetof(struct np_record,digest));sha256_final(&h,out);}
static int np_operation(const char*t){return t&&strlen(t)==32&&strspn(t,"0123456789abcdef")==32;}
#include "stage_channel.h"
static int np_valid(const struct np_record*r){unsigned char d[32];if(r->magic!=NP_MAGIC||(r->version!=2&&r->version!=3)||r->bytes!=sizeof *r||!r->sequence||r->pid<2||r->mono_nsec>=1000000000u||!np_text(r->phase,49,1)||!np_text(r->state,49,1)||!np_text(r->detail,1025,0)||!np_text(r->stamp,65,0))return 0;
 if(r->version==3){if(r->operation[32]||r->operation[33]||r->operation[34]||r->operation[35]||!np_operation(r->operation))return 0;}else for(unsigned i=0;i<sizeof r->operation;i++)if(r->operation[i])return 0;
 for(unsigned i=0;i<sizeof r->reserved;i++)if(r->reserved[i])return 0;np_digest(r,d);return !memcmp(d,r->digest,32);}
static int np_last_fd(int fd,struct np_record*out){
 struct stat st;if(NP_FSTAT(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<2048||st.st_size>64*1024*1024)return 0;
 off_t at=st.st_size/2048*2048;for(unsigned n=0;at>=2048&&n<64;n++){at-=2048;if(pread(fd,out,sizeof *out,at)==sizeof *out&&np_valid(out))return 1;}return 0;
}
static int np_last(const char*path,struct np_record*out){int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;int ok=np_last_fd(fd,out);if(close(fd))ok=0;return ok;}
static int np_publish(const char*path,const char*phase,const char*state,const char*detail){
 struct np_record r={0},old;if(!np_text(phase,49,1)||!np_text(state,49,1)||!np_text(detail,1025,0))return 0;
 int fd=open(path,O_RDWR|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;struct stat st;int ok=!flock(fd,LOCK_EX)&&!NP_FSTAT(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>=0&&st.st_size<64*1024*1024;
 if(ok&&st.st_size%2048)ok=!ftruncate(fd,st.st_size/2048*2048);
 r.magic=NP_MAGIC;r.version=2;r.bytes=sizeof r;r.sequence=np_last_fd(fd,&old)?old.sequence+1:1;r.pid=(uint32_t)getpid();
 const char*operation=getenv("NX51_PROGRESS_TOKEN");if(np_operation(operation)){r.version=3;strcpy(r.operation,operation);}
 struct timespec mono;if(clock_gettime(CLOCK_MONOTONIC,&mono))ok=0;else{r.mono_sec=(uint32_t)mono.tv_sec;r.mono_nsec=(uint32_t)mono.tv_nsec;}
 strcpy(r.phase,phase);strcpy(r.state,state);strcpy(r.detail,detail);strcpy(r.stamp,"UNAVAILABLE");np_digest(&r,r.digest);
 if(ok)ok=np_valid(&r)&&write(fd,&r,sizeof r)==sizeof r&&!fsync(fd);if(close(fd))ok=0;if(np_valid(&r))np_stage_send(&r);return ok;
}
#endif
