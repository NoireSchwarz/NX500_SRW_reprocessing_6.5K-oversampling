/* SPDX-License-Identifier: AGPL-3.0-only. Small durable facts, never pixels.
 * A phase describes an operation that may have run. Only an explicit verified
 * return event closes a CAP; process exit and error names never close one. */
#ifndef NX_RESOURCE_LEDGER_H
#define NX_RESOURCE_LEDGER_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/file.h>
#define NR_MAGIC 0x3152534eu
#define NR_MAX_RECORDS 64u
enum nr_phase {NR_PREPARED=1,NR_ALLOCATING,NR_REQUEST_RETURNED,NR_OWNED,NR_FRONT_GO,NR_FRONT_DONE,NR_RELEASED,NR_STOPPED};
struct nr_resource {
 uint32_t magic,version,bytes,sequence,phase,capacity,bank,kind,owner_pid,ticks_lo,ticks_hi;
 uint32_t request_started,response_valid,response_status,response[5];
 uint32_t ownership_verified,index,metadata[4],nonce,native_calls,mcb_sends,slot_writes,go,done,code_restored,detached,api_inflight,paused;
 uint32_t reserved[28],checksum;
};
_Static_assert(sizeof(struct nr_resource)==256,"resource ledger record ABI");
static uint32_t nr_checksum(const void*p,size_t n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int nr_valid(const struct nr_resource*r){
 if(r->magic!=NR_MAGIC||r->version!=1||r->bytes!=sizeof *r||!r->sequence||r->sequence>NR_MAX_RECORDS||r->phase<NR_PREPARED||r->phase>NR_STOPPED||
  r->capacity<134217728u||r->capacity>268435456u||(r->capacity&16383u)||r->bank!=2||r->kind!=35||r->owner_pid<2||(!r->ticks_lo&&!r->ticks_hi)||
  r->request_started>1||r->response_valid>1||r->ownership_verified>1||r->go>1||r->done>1||r->code_restored>1||r->detached>1||r->api_inflight>1||r->paused>1||
  (r->response_valid&&!r->request_started)||(r->ownership_verified&&(!r->response_valid||!r->response[0]||r->response[1]<0x94000000u||(r->response[1]&16383u)||(uint64_t)r->response[1]+r->capacity>0xc0000000ULL||r->response[2]!=r->capacity||r->response[3]!=2||r->index!=(r->response[1]-0x94000000u)/16384u||(r->metadata[0]&255u)!=35||r->metadata[1]!=r->capacity/16384u))||(r->go&&!r->ownership_verified)||(r->done&&!r->go)||
  r->checksum!=nr_checksum(r,offsetof(struct nr_resource,checksum)))return 0;
 for(unsigned i=0;i<28;i++)if(r->reserved[i])return 0;return 1;
}
static int nr_read_fd(int fd,struct nr_resource*out){
 struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<256||st.st_size>NR_MAX_RECORDS*256u||st.st_size%256)return 0;
 struct nr_resource first={0},r;for(unsigned i=0;i<(uint64_t)st.st_size/256u;i++){
  if(pread(fd,&r,sizeof r,(off_t)i*256)!=(ssize_t)sizeof r||!nr_valid(&r)||r.sequence!=i+1)return 0;
  if(!i)first=r;else if(r.owner_pid!=first.owner_pid||r.ticks_lo!=first.ticks_lo||r.ticks_hi!=first.ticks_hi||r.capacity!=first.capacity||r.bank!=first.bank||r.kind!=first.kind)return 0;
 }
 *out=r;return 1;
}
static int nr_read(const char*path,struct nr_resource*out){int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;int ok=nr_read_fd(fd,out);if(close(fd))ok=0;return ok;}
static int nr_append(const char*path,struct nr_resource*r){
 int fd=open(path,O_RDWR|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;struct stat st;int ok=!flock(fd,LOCK_EX)&&!fstat(fd,&st)&&S_ISREG(st.st_mode);
 struct nr_resource last;if(ok&&st.st_size){ok=nr_read_fd(fd,&last)&&last.sequence==r->sequence&&last.owner_pid==r->owner_pid&&last.ticks_lo==r->ticks_lo&&last.ticks_hi==r->ticks_hi&&last.capacity==r->capacity;}
 else if(ok&&r->sequence)ok=0;
 if(ok){r->magic=NR_MAGIC;r->version=1;r->bytes=sizeof *r;r->sequence++;r->checksum=nr_checksum(r,offsetof(struct nr_resource,checksum));ok=nr_valid(r)&&write(fd,r,sizeof *r)==sizeof *r&&!fsync(fd);}
 if(close(fd))ok=0;return ok;
}
#endif
