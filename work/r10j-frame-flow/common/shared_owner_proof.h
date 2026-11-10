/* SPDX-License-Identifier: AGPL-3.0-only.
 * A live front is bound to the kernel executable identity it had when it
 * published this exact allocation. Keep the 512-byte lease ABI unchanged.
 * Historical lease files stay readable; live producers must publish a proof.
 */
#ifndef NXL_SHARED_OWNER_PROOF_H
#define NXL_SHARED_OWNER_PROOF_H
#define SW_OWNER_MAGIC 0x31504f53u
struct sw_owner_proof {
 uint32_t magic,version,bytes,reserved;
 uint64_t executable_device,executable_inode;
 struct sw_lease allocation;
 uint32_t checksum,tail;
};
_Static_assert(sizeof(struct sw_owner_proof)==552,"fixed owner proof ABI");
static const char*sw_owner_error="FRONT_OWNER_NOT_CHECKED";
static int sw_owner_path(const struct sw_lease*s,char path[160]){
 int n=snprintf(path,160,"/tmp/nx-lut-lite/shared-owner-%u-%08x.bin",s->owner_pid,s->nonce);return n>0&&n<160;
}
static void sw_owner_normalize(struct sw_lease*s){s->phase=0;s->checksum=0;}
static int sw_owner_executable(uint32_t pid,uint64_t*device,uint64_t*inode){
 char path[80];snprintf(path,sizeof path,"/proc/%u/exe",pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 int ok=!sw_fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_ino!=0;
 if(ok){*device=(uint64_t)st.st_dev;*inode=(uint64_t)st.st_ino;}
 if(close(fd))ok=0;return ok;
}
static int sw_owner_proof_valid(const struct sw_owner_proof*p,const struct sw_lease*s){
 if(p->magic!=SW_OWNER_MAGIC||p->version!=1||p->bytes!=sizeof *p||p->reserved||p->tail||!p->executable_inode||p->checksum!=sw_checksum(p,offsetof(struct sw_owner_proof,checksum)))return 0;
 struct sw_lease normalized=*s;sw_owner_normalize(&normalized);return !memcmp(&normalized,&p->allocation,sizeof normalized);
}
static int sw_owner_proof_publish(const struct sw_lease*s){
 uint64_t ticks=0;struct sw_owner_proof proof;memset(&proof,0,sizeof proof);
 if(!sw_valid(s)||s->owner_pid!=(uint32_t)getpid()||!sw_owner_ticks(s->owner_pid,&ticks)||ticks!=s->owner_start_ticks||!sw_owner_executable(s->owner_pid,&proof.executable_device,&proof.executable_inode))return 0;
 proof.magic=SW_OWNER_MAGIC;proof.version=1;proof.bytes=sizeof proof;proof.allocation=*s;sw_owner_normalize(&proof.allocation);proof.checksum=sw_checksum(&proof,offsetof(struct sw_owner_proof,checksum));
 char path[160];return sw_owner_path(s,path)&&sw_file_new(path,&proof,sizeof proof);
}
static int sw_owner_alive(const struct sw_lease*s){
 uint64_t ticks=0;sw_owner_error="FRONT_OWNER_PID_OR_START_CHANGED";
 if(!sw_valid(s)||!sw_owner_ticks(s->owner_pid,&ticks)||ticks!=s->owner_start_ticks)return 0;
 char path[160];if(!sw_owner_path(s,path))return 0;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(fd<0){
  sw_owner_error=errno==ENOENT?"FRONT_OWNER_PROOF_MISSING":"FRONT_OWNER_PROOF_UNREADABLE";return 0;
 }
 struct stat st;struct sw_owner_proof proof;size_t at=0;
 int ok=!sw_fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)sizeof proof;
 while(ok&&at<sizeof proof){ssize_t n=read(fd,(unsigned char*)&proof+at,sizeof proof-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(close(fd))ok=0;
 if(!ok||!sw_owner_proof_valid(&proof,s)){sw_owner_error="FRONT_OWNER_PROOF_ALLOCATION_MISMATCH";return 0;}
 for(unsigned i=0;i<2;i++){
  uint64_t dev=0,ino=0;
  if(!sw_owner_executable(s->owner_pid,&dev,&ino)||dev!=proof.executable_device||ino!=proof.executable_inode){sw_owner_error="FRONT_OWNER_EXECUTABLE_CHANGED";return 0;}
 }
 if(!sw_owner_ticks(s->owner_pid,&ticks)||ticks!=s->owner_start_ticks)return 0;
 sw_owner_error="FRONT_OWNER_ALLOCATION_AND_EXECUTABLE_VERIFIED";return 1;
}
static int sw_owner_proof_retire(const struct sw_lease*s){
 if(!sw_valid(s)||s->owner_pid!=(uint32_t)getpid())return 0;
 char path[160];if(!sw_owner_path(s,path))return 0;
 struct stat st;if(lstat(path,&st))return errno==ENOENT;
 if(!sw_owner_alive(s))return 0;return !unlink(path);
}
#endif
