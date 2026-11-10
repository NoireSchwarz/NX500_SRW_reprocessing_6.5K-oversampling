/* SPDX-License-Identifier: AGPL-3.0-only.
 * Reuse the producer's complete persisted-file readback within publication.
 * Default explicit --verify-output still reads all pixels. Cache misses use
 * that complete path; this ticket has no resource-release authority.
 */
#ifndef NXL_JPEG_TICKET_H
#define NXL_JPEG_TICKET_H
struct jt_ticket {
 uint32_t magic,version,bytes,nonce,pa,owner_pid,stream_bytes,verified;
 uint64_t device,inode,size;int64_t mtime,ctime;uint32_t mtime_ns,ctime_ns;
 char jpeg_sha256[68],source_sha256[68];uint32_t reserved[201],checksum;
};
_Static_assert(sizeof(struct jt_ticket)==1024,"JPEG readback ticket");
static uint32_t jt_sum(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int jt_hex(const char*p){for(unsigned i=0;i<64;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return !p[64]&&!p[65]&&!p[66]&&!p[67];}
static void jt_stat(struct jt_ticket*t,const struct stat*s){t->device=(uint64_t)s->st_dev;t->inode=(uint64_t)s->st_ino;t->size=(uint64_t)s->st_size;t->mtime=(int64_t)s->st_mtim.tv_sec;t->ctime=(int64_t)s->st_ctim.tv_sec;t->mtime_ns=(uint32_t)s->st_mtim.tv_nsec;t->ctime_ns=(uint32_t)s->st_ctim.tv_nsec;}
static int jt_same_stat(const struct stat*a,const struct stat*b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int jt_matches(const struct jt_ticket*t,const struct stat*s,uint32_t nonce,uint32_t pa,uint32_t owner,const char*hash,const char*source){
 if(t->magic!=0x3154474au||t->version!=1||t->bytes!=sizeof *t||t->verified!=1||!t->nonce||t->nonce!=nonce||t->pa!=pa||t->owner_pid!=owner||t->stream_bytes<32||t->stream_bytes!=t->size||t->checksum!=jt_sum(t,offsetof(struct jt_ticket,checksum))||!jt_hex(t->jpeg_sha256)||!jt_hex(t->source_sha256)||memcmp(t->jpeg_sha256,hash,64)||memcmp(t->source_sha256,source,64)||!S_ISREG(s->st_mode))return 0;
 if(t->device!=(uint64_t)s->st_dev||t->inode!=(uint64_t)s->st_ino||t->size!=(uint64_t)s->st_size||t->mtime!=(int64_t)s->st_mtim.tv_sec||t->ctime!=(int64_t)s->st_ctim.tv_sec||t->mtime_ns!=(uint32_t)s->st_mtim.tv_nsec||t->ctime_ns!=(uint32_t)s->st_ctim.tv_nsec)return 0;
 for(unsigned i=0;i<201;i++)if(t->reserved[i])return 0;return 1;
}
static int jt_read(const char*path,struct jt_ticket*t){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;unsigned done=0;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==sizeof *t;
 while(ok&&done<sizeof *t){ssize_t n=read(fd,(unsigned char*)t+done,sizeof *t-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(unsigned)n;}if(close(fd))ok=0;return ok;
}
static int jt_write(const char*path,const struct stat*st,uint32_t nonce,uint32_t pa,uint32_t owner,const char*hash,const char*source){
 struct jt_ticket t={0};t.magic=0x3154474au;t.version=1;t.bytes=sizeof t;t.nonce=nonce;t.pa=pa;t.owner_pid=owner;t.stream_bytes=(uint32_t)st->st_size;t.verified=1;jt_stat(&t,st);
 memcpy(t.jpeg_sha256,hash,64);memcpy(t.source_sha256,source,64);t.checksum=jt_sum(&t,offsetof(struct jt_ticket,checksum));
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;unsigned done=0;int ok=1;
 while(done<sizeof t){ssize_t n=write(fd,(const unsigned char*)&t+done,sizeof t-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(unsigned)n;}if(fsync(fd))ok=0;if(close(fd))ok=0;return ok;
}
#endif
