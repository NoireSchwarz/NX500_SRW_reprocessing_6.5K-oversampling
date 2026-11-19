/* R11f6: read-only activation proof and bounded snapshot backup.
 * Never ptrace, write process memory, open /dev/mem, or write a block device. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
_Static_assert(sizeof(size_t)==4, "NX500 requires ARM32 size_t");
_Static_assert(BLKGETSIZE64==0x80041272u, "NX500 block ioctl ABI");
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>
#include "sha256.h"
#include "video_hevc_gate.h"
#include "video_hevc_gate_identity.h"
#define CORE "/usr/lib/libmmfcore.so"
#define APP "/usr/apps/com.samsung.di-camera-app/bin/di-camera-app"
#define PATCH "7743dd26b06fb2e600cc65aff6ad92913b43c3f75b9493437239006b24991cd9"
#define OEM "b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8"
static int failure(const char *s){printf("NXL_R11F6_CHECK1\nstate=UNAVAILABLE\nreason=%s\nend=NXL_R11F6_CHECK_END1\n",s);return 1;}
static void hex(unsigned char*d,char*out){static const char a[]="0123456789abcdef";for(unsigned i=0;i<32;i++){out[2*i]=a[d[i]>>4];out[2*i+1]=a[d[i]&15];}out[64]=0;}
static int hashfd(int fd,uint64_t limit,char*out){
 struct sha256 h;sha256_init(&h);unsigned char b[32768],d[32];uint64_t n=0;ssize_t got;
 while((got=read(fd,b,sizeof b))>0){if(n+(unsigned)got>limit)return 0;n+=(unsigned)got;sha256_update(&h,b,(size_t)got);}
 if(got<0||n!=limit)return 0;sha256_final(&h,d);hex(d,out);return 1;
}
static int filehash(const char*path,char*out,struct stat*st){
 int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;struct stat after;
 int ok=!fstat(f,st)&&S_ISREG(st->st_mode)&&st->st_size>=0&&st->st_size<=1048576&&hashfd(f,(uint64_t)st->st_size,out)&&!fstat(f,&after)&&st->st_ino==after.st_ino&&st->st_dev==after.st_dev&&st->st_size==after.st_size;
 if(close(f))ok=0;return ok;
}
static uint64_t ticks(unsigned pid){
 char path[64],b[2048];snprintf(path,sizeof path,"/proc/%u/stat",pid);int f=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;
 ssize_t n=read(f,b,sizeof b-1);int ok=n>0&&n<(ssize_t)sizeof b-1;if(close(f))ok=0;if(!ok)return 0;b[n]=0;char*q=strrchr(b,')');if(!q||q[1]!=' ')return 0;q+=2;
 for(unsigned i=3;i<22;i++){q=strchr(q,' ');if(!q)return 0;while(*q==' ')q++;}char*end;uint64_t t=strtoull(q,&end,10);return end!=q&&(*end==' '||*end=='\n')?t:0;
}
static int gate(unsigned pid,int original){
 char path[128],target[256],digest[65];struct stat st,visible;uint64_t start=ticks(pid);if(!start)return failure("CAMERA_IDENTITY_UNAVAILABLE");
 snprintf(path,sizeof path,"/proc/%u/exe",pid);ssize_t n=readlink(path,target,sizeof target-1);if(n<0||n>=(ssize_t)sizeof target-1)return failure("CAMERA_EXE_UNAVAILABLE");target[n]=0;if(strcmp(target,APP))return failure("CAMERA_EXE_MISMATCH");
 if(!filehash(CORE,digest,&st))return failure("CORE_FILE_UNREADABLE");
 if(original){if(strcmp(digest,OEM)||st.st_size!=340268)return failure("OEM_CORE_FILE_MISMATCH");}
 else {if(!strcmp(digest,OEM))return failure("CORE_FILE_IS_OEM");if(strcmp(digest,PATCH)||st.st_size!=434176)return failure("CORE_FILE_UNKNOWN");}
 snprintf(path,sizeof path,"/proc/%u/root/usr/lib/libmmfcore.so",pid);if(!filehash(path,digest,&visible)||strcmp(digest,original?OEM:PATCH)||visible.st_dev!=st.st_dev||visible.st_ino!=st.st_ino)return failure("CAMERA_VISIBLE_CORE_MISMATCH");
 snprintf(path,sizeof path,"/proc/%u/maps",pid);FILE*f=fopen(path,"r");if(!f)return failure("CAMERA_MAPS_UNAVAILABLE");
 char line[1200];uint32_t base=0;unsigned bases=0,rx=0,rw=0;struct {uint32_t a,b;uint64_t off,ino;unsigned major,minor;char mode[8];}m[16];unsigned count=0;
 while(fgets(line,sizeof line,f)){
  unsigned a,b,devmajor,devminor;unsigned long long off,ino;char mode[8],name[512];
  if(sscanf(line,"%x-%x %7s %llx %x:%x %llu %511[^\n]",&a,&b,mode,&off,&devmajor,&devminor,&ino,name)!=8||strcmp(name,CORE))continue;
  if(count>=16){fclose(f);return failure("CORE_MAP_COUNT_INVALID");}m[count].a=a;m[count].b=b;m[count].off=off;m[count].ino=ino;m[count].major=devmajor;m[count].minor=devminor;strcpy(m[count++].mode,mode);
  if(!off&&!strcmp(mode,"r-xp")&&ino==(uint64_t)st.st_ino&&devmajor==major(st.st_dev)&&devminor==minor(st.st_dev)){base=a;bases++;}
 }
 int ok=!ferror(f);if(fclose(f))ok=0;if(!ok||bases!=1||base>0xbff00000u-HG_STATE)return failure("CORE_BASE_INVALID");
 for(unsigned i=0;i<count;i++){
  if(m[i].ino!=(uint64_t)st.st_ino||m[i].major!=major(st.st_dev)||m[i].minor!=minor(st.st_dev))continue;
  if(m[i].a==base+HG_CODE&&m[i].b>=base+HG_CODE+HG_IDENTITY_WORDS*4u&&m[i].off==HG_CODE&&!strcmp(m[i].mode,"r-xp"))rx++;
  if(m[i].a==base+HG_STATE&&m[i].b>=base+HG_STATE+0x2000u&&m[i].off==HG_STATE&&!strcmp(m[i].mode,"rw-p"))rw++;
 }
 if(!original&&(rx!=1||rw!=1))return failure("NEW_GATE_SEGMENTS_NOT_LOADED");
 if(original&&(rx||rw))return failure("OEM_CORE_HAS_GATE_SEGMENTS");
 snprintf(path,sizeof path,"/proc/%u/mem",pid);int mem=open(path,O_RDONLY|O_CLOEXEC);if(mem<0)return failure("CAMERA_MEMORY_UNREADABLE");
 uint32_t id[16],code[HG_IDENTITY_WORDS],call=0,cmd[16],again[16];
 if(original){
  ok=pread(mem,&call,4,(off_t)((uint64_t)base+HG_CALL))==4&&call==(0xeb000000u|(((0x13998u-HG_CALL-8u)>>2)&0xffffffu));
  if(close(mem))ok=0;if(!ok||ticks(pid)!=start)return failure("OEM_LIVE_BYTES_MISMATCH");
  printf("NXL_R11F6_CHECK1\nstate=OEM_RESTORED\nreason=LIVE_OEM_CORE_VERIFIED\npid=%u\nticks=%llu\nend=NXL_R11F6_CHECK_END1\n",pid,(unsigned long long)start);return 0;
 }
 ok=pread(mem,id,sizeof id,(off_t)((uint64_t)base+HG_STATE))==sizeof id&&!memcmp(id,hg_identity,sizeof id)&&pread(mem,code,sizeof code,(off_t)((uint64_t)base+HG_CODE))==sizeof code&&hg_hash(code,HG_IDENTITY_WORDS)==HG_IDENTITY_FNV;
 if(ok)ok=pread(mem,&call,4,(off_t)((uint64_t)base+HG_CALL))==4&&call==(0xeb000000u|(((HG_CODE-HG_CALL-8u)>>2)&0xffffffu));
 if(ok)ok=pread(mem,cmd,sizeof cmd,(off_t)((uint64_t)base+HG_STATE+HG_COMMAND))==sizeof cmd&&pread(mem,again,sizeof again,(off_t)((uint64_t)base+HG_STATE+HG_COMMAND))==sizeof again&&!memcmp(cmd,again,sizeof cmd);
 if(close(mem))ok=0;if(!ok)return failure("LIVE_GATE_BYTES_MISMATCH");if(cmd[3]||cmd[0]&1u)return failure("GATE_COMMAND_NOT_DISABLED");if(ticks(pid)!=start)return failure("CAMERA_CHANGED_DURING_CHECK");
 printf("NXL_R11F6_CHECK1\nstate=ACTIVE\nreason=LIVE_CORE_AND_GATE_VERIFIED\npid=%u\nticks=%llu\nbase=%08x\ninode=%llu\ncommand_enabled=0\nend=NXL_R11F6_CHECK_END1\n",pid,(unsigned long long)start,base,(unsigned long long)st.st_ino);return 0;
}
/* Persist milestones before advancing; stdout is the new SD report file. */
static int backup_log(void){return fflush(stdout)==0 && fsync(STDOUT_FILENO)==0;}
static int backup_fail(const char *stage,int saved_errno,long long rc,uint64_t bytes,int src,int dst){
 if(dst>=0)close(dst);if(src>=0)close(src);
 printf("backup_stage=%s\nstate=FAILED\nerrno=%d\noperation_rc=%lld\nbytes_processed=%llu\ncomplete=0\npartial_file_preserved=1\n",stage,saved_errno,rc,(unsigned long long)bytes);
 (void)backup_log();return 1;
}
static int backup_hash(int fd,uint64_t size,char*out,const char *stage,uint64_t *done){
 struct sha256 h;sha256_init(&h);unsigned char b[32768],d[32];uint64_t next=16u*1024u*1024u;*done=0;
 while(*done<size){unsigned want=size-*done>sizeof b?sizeof b:(unsigned)(size-*done);ssize_t n=read(fd,b,want);if(n<=0){if(n==0)errno=EIO;return 0;}sha256_update(&h,b,(size_t)n);*done+=(unsigned)n;
  if(*done>=next||*done==size){printf("backup_stage=%s\nprogress_bytes=%llu\ntotal_bytes=%llu\n",stage,(unsigned long long)*done,(unsigned long long)size);if(!backup_log())return 0;next+=16u*1024u*1024u;}
 }
 sha256_final(&h,d);hex(d,out);return 1;
}
static int backup(const char*path){
 const char*prefix="/mnt/mmc/NXDEMO51/R11F6_ACTIVATION_";
 if(strncmp(path,prefix,strlen(prefix))||strstr(path,"..")||strlen(path)>240||strlen(path)<strlen(prefix)+24)return failure("BACKUP_PATH_INVALID");
 for(const char*q=path;*q;q++)if(!((*q>='A'&&*q<='Z')||(*q>='a'&&*q<='z')||(*q>='0'&&*q<='9')||strchr("_./-",*q)))return failure("BACKUP_PATH_INVALID");
 setvbuf(stdout,NULL,_IONBF,0);
 printf("NXL_R11F6_SNAPSHOT2\nbackup_stage=OPEN_SOURCE\nsource=/dev/mmcblk0p9\nsource_open=READ_ONLY\nioctl_request=%08x\n",(unsigned)BLKGETSIZE64);
 if(!backup_log())return 1;
 int src=open("/dev/mmcblk0p9",O_RDONLY|O_CLOEXEC|O_NOFOLLOW),dst=-1;struct stat st;uint64_t size=0,done=0;int rc;
 if(src<0)return backup_fail("OPEN_SOURCE",errno,src,0,-1,-1);
 rc=fstat(src,&st);if(rc)return backup_fail("FSTAT_SOURCE",errno,rc,0,src,-1);
 printf("backup_stage=DEVICE_IDENTITY\nmode=%08x\ndevice_major=%u\ndevice_minor=%u\n",(unsigned)st.st_mode,major(st.st_rdev),minor(st.st_rdev));
 if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,0,src,-1);
 if(!S_ISBLK(st.st_mode)||major(st.st_rdev)!=179||minor(st.st_rdev)!=9)return backup_fail("DEVICE_IDENTITY",0,-1,0,src,-1);
 errno=0;rc=ioctl(src,BLKGETSIZE64,&size);int ioe=errno;
 printf("backup_stage=DEVICE_SIZE\nioctl_rc=%d\nioctl_errno=%d\nreported_bytes=%llu\n",rc,ioe,(unsigned long long)size);
 if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,0,src,-1);
 if(rc)return backup_fail("DEVICE_SIZE_IOCTL",ioe,rc,0,src,-1);
 /* OEM p9 is 307199 sectors (150 MiB - 512), not page aligned. */
 if(size<67108864u||size>268435456u||(size&511u))return backup_fail("DEVICE_SIZE_RANGE",0,-1,0,src,-1);
 unsigned char head[4096];ssize_t got=pread(src,head,sizeof head,0);
 if(got!=sizeof head)return backup_fail("HEADER_READ",got<0?errno:0,got,0,src,-1);
 const char*kind=!memcmp(head+4086,"S1SUSPEND",10)?"S1SUSPEND":!memcmp(head+4086,"SWAPSPACE2",10)?"SWAPSPACE2":!memcmp(head+4086,"SWAP-SPACE",10)?"SWAP-SPACE":"UNKNOWN";
 printf("backup_stage=HEADER\nsignature_kind=%s\nsignature_hex=",kind);for(unsigned i=4086;i<4096;i++)printf("%02x",head[i]);printf("\nbackup_scope=PARTITION_BYTES_NOT_RESUMABILITY_GUARANTEE\n");
 if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,0,src,-1);
 if(!strcmp(kind,"UNKNOWN"))return backup_fail("HEADER_SIGNATURE",0,-1,0,src,-1);
 dst=open(path,O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(dst<0)return backup_fail("OPEN_TARGET",errno,dst,0,src,-1);
 struct sha256 h;sha256_init(&h);unsigned char b[32768],d[32];uint64_t total=0,next=16u*1024u*1024u;
 printf("backup_stage=COPY\ntotal_bytes=%llu\n",(unsigned long long)size);if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,0,src,dst);
 while(total<size){unsigned want=size-total>sizeof b?sizeof b:(unsigned)(size-total);ssize_t n=read(src,b,want);if(n<=0)return backup_fail("COPY_READ",n<0?errno:0,n,total,src,dst);
  unsigned at=0;while(at<(unsigned)n){ssize_t k=write(dst,b+at,(unsigned)n-at);if(k<=0)return backup_fail("COPY_WRITE",k<0?errno:0,k,total+at,src,dst);at+=(unsigned)k;}
  sha256_update(&h,b,(size_t)n);total+=(unsigned)n;
  if(total>=next||total==size){printf("backup_stage=COPY\nprogress_bytes=%llu\ntotal_bytes=%llu\n",(unsigned long long)total,(unsigned long long)size);if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,total,src,dst);next+=16u*1024u*1024u;}
 }
 char first[65],stored[65],source[65];sha256_final(&h,d);hex(d,first);
 rc=fsync(dst);if(rc)return backup_fail("TARGET_FSYNC",errno,rc,total,src,dst);
 if(lseek(dst,0,SEEK_SET)!=0)return backup_fail("TARGET_REWIND",errno,-1,total,src,dst);
 printf("backup_stage=VERIFY_BACKUP\n");if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,total,src,dst);
 if(!backup_hash(dst,size,stored,"VERIFY_BACKUP",&done))return backup_fail("VERIFY_BACKUP_READ",errno,-1,done,src,dst);
 if(strcmp(first,stored))return backup_fail("VERIFY_BACKUP_HASH",0,-1,done,src,dst);
 if(lseek(src,0,SEEK_SET)!=0)return backup_fail("SOURCE_REWIND",errno,-1,total,src,dst);
 printf("backup_stage=VERIFY_SOURCE\n");if(!backup_log())return backup_fail("LOG_SYNC",errno,-1,total,src,dst);
 if(!backup_hash(src,size,source,"VERIFY_SOURCE",&done))return backup_fail("VERIFY_SOURCE_READ",errno,-1,done,src,dst);
 if(strcmp(first,source))return backup_fail("VERIFY_SOURCE_HASH",0,-1,done,src,dst);
 rc=close(dst);dst=-1;if(rc)return backup_fail("CLOSE_TARGET",errno,rc,total,src,-1);
 rc=close(src);src=-1;if(rc)return backup_fail("CLOSE_SOURCE",errno,rc,total,-1,-1);
 printf("backup_stage=COMPLETE\ncomplete=1\nbytes=%llu\nsha256=%s\nsource_open=READ_ONLY\nsource_reverified=1\nbackup_reverified=1\n",(unsigned long long)size,first);
 return backup_log()?0:1;
}
int main(int argc,char**argv){
 if(argc!=3)return failure("ARGUMENT_INVALID");if(!strcmp(argv[1],"--backup-snapshot"))return backup(argv[2]);
 if(strcmp(argv[1],"--check")&&strcmp(argv[1],"--check-oem"))return failure("ARGUMENT_INVALID");char*e;unsigned long pid=strtoul(argv[2],&e,10);if(*e||pid<=1||pid>2147483647u)return failure("PID_INVALID");return gate((unsigned)pid,!strcmp(argv[1],"--check-oem"));
}
