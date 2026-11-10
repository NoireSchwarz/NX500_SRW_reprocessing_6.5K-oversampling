/* SPDX-License-Identifier: AGPL-3.0-only.
 * One invocation's full source digest, bound to the same stat identity and
 * session operation. RAM only. A miss falls back to a complete source hash.
 */
#ifndef NXL_SOURCE_TICKET_H
#define NXL_SOURCE_TICKET_H
#define STKT_MAGIC 0x31544b53u
struct stkt_ticket {
 uint32_t magic,version,bytes,producer_pid;
 uint64_t device,inode,size;int64_t mtime,ctime;uint32_t mtime_ns,ctime_ns;
 char source[512],sha256[68],operation[36];uint32_t reserved[85],checksum;
};
_Static_assert(sizeof(struct stkt_ticket)==1024,"source ticket wire size");
static uint32_t stkt_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int stkt_operation(const char*p){if(!p||strlen(p)!=32)return 0;for(unsigned i=0;i<32;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return 1;}
static int stkt_path(const char*p,const char*operation){
 const char*prefix="/tmp/nx-lut-lite/source-ticket-";size_t n=strlen(prefix);
 if(!p||!stkt_operation(operation)||strlen(p)>180||strncmp(p,prefix,n)||strncmp(p+n,operation,32)||p[n+32]!='-')return 0;
 const char*q=p+n+33;unsigned digits=0;while(*q>='0'&&*q<='9'){q++;digits++;}return digits&&digits<=10&&!strcmp(q,".bin");
}
static int stkt_hex(const char*p){for(unsigned i=0;i<64;i++)if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')))return 0;return !p[64]&&!p[65]&&!p[66]&&!p[67];}
static void stkt_stat(struct stkt_ticket*t,const struct stat*s){t->device=(uint64_t)s->st_dev;t->inode=(uint64_t)s->st_ino;t->size=(uint64_t)s->st_size;t->mtime=(int64_t)s->st_mtim.tv_sec;t->ctime=(int64_t)s->st_ctim.tv_sec;t->mtime_ns=(uint32_t)s->st_mtim.tv_nsec;t->ctime_ns=(uint32_t)s->st_ctim.tv_nsec;}
static int stkt_same(const struct stkt_ticket*t,const struct stat*s){return t->device==(uint64_t)s->st_dev&&t->inode==(uint64_t)s->st_ino&&t->size==(uint64_t)s->st_size&&t->mtime==(int64_t)s->st_mtim.tv_sec&&t->ctime==(int64_t)s->st_ctim.tv_sec&&t->mtime_ns==(uint32_t)s->st_mtim.tv_nsec&&t->ctime_ns==(uint32_t)s->st_ctim.tv_nsec;}
static int stkt_load(const char*path,const char*operation,const char*source,const char*expected,const struct stat*identity,char out[65]){
 if(!stkt_path(path,operation)||!source||!expected||strlen(source)>=512||strlen(expected)!=64)return 0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;struct stkt_ticket t;unsigned done=0;int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==sizeof t;
 while(ok&&done<sizeof t){ssize_t n=read(fd,(unsigned char*)&t+done,sizeof t-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(unsigned)n;}if(close(fd))ok=0;
 if(!ok||t.magic!=STKT_MAGIC||t.version!=1||t.bytes!=sizeof t||t.checksum!=stkt_hash(&t,offsetof(struct stkt_ticket,checksum))||!t.producer_pid||!memchr(t.source,0,sizeof t.source)||!memchr(t.operation,0,sizeof t.operation)||strcmp(t.source,source)||strcmp(t.operation,operation)||!stkt_hex(t.sha256)||memcmp(t.sha256,expected,64)||!stkt_same(&t,identity))return 0;
 for(unsigned i=0;i<85;i++)if(t.reserved[i])return 0;for(unsigned i=33;i<36;i++)if(t.operation[i])return 0;
 memcpy(out,t.sha256,65);return 1;
}
static int stkt_save(const char*path,const char*operation,const char*source,const char*hash,const struct stat*identity){
 if(!stkt_path(path,operation)||!source||strlen(source)>=512||!hash||strlen(hash)!=64)return 0;
 struct stkt_ticket t={0};t.magic=STKT_MAGIC;t.version=1;t.bytes=sizeof t;t.producer_pid=(uint32_t)getpid();stkt_stat(&t,identity);strcpy(t.source,source);strcpy(t.sha256,hash);strcpy(t.operation,operation);t.checksum=stkt_hash(&t,offsetof(struct stkt_ticket,checksum));
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;unsigned done=0;int ok=1;
 while(done<sizeof t){ssize_t n=write(fd,(const unsigned char*)&t+done,sizeof t-done);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}done+=(unsigned)n;}if(fsync(fd))ok=0;if(close(fd))ok=0;return ok;
}
#endif
