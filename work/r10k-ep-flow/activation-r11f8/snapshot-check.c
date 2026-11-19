/* R11f8: exact-length, read-only streaming verification of regular snapshots.
 * This is separate from patch-fileops' 128 MiB package-file contract. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sha256.h"
#define SNAPSHOT_MAX_BYTES (256ull * 1024ull * 1024ull)
_Static_assert(sizeof(size_t)==4, "NX500 ARM32 required");
static int identity(const struct stat *a,const struct stat *b){
 return S_ISREG(a->st_mode)&&S_ISREG(b->st_mode)&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void stage(const char *s){printf("stage=%s\n",s);fflush(stdout);}
int main(int argc,char **argv){
 int rc=2,err=0,fd=-1;const char *why="ARGUMENT_INVALID";uint64_t expected=0,total=0,next=16ull*1024ull*1024ull;unsigned calls=0,last=0;
 struct stat named,before,after,final_name;long long size_before=-1,size_after=-1;char actual[65]="UNAVAILABLE";
 printf("NXL_R11F8_SNAPSHOT_CHECK1\nopen_mode=READ_ONLY_NOFOLLOW_NONBLOCK\nmax_bytes=%llu\n",(unsigned long long)SNAPSHOT_MAX_BYTES);
 stage("ARGUMENTS");
 if(argc!=5||strcmp(argv[1],"--verify"))goto done;
 if(argv[2][0]!='/'||strlen(argv[2])>1023||strpbrk(argv[2],"\r\n\t")) {why="PATH_INVALID";goto done;}
 printf("path=%s\nexpected_size_text=%s\nexpected_sha256=%s\n",argv[2],argv[3],argv[4]);
 if(!argv[3][0])goto done;
 for(const char *p=argv[3];*p;p++)if(*p<'0'||*p>'9')goto done;
 errno=0;char *end;expected=strtoull(argv[3],&end,10);
 if(errno||*end||expected<4096||expected>SNAPSHOT_MAX_BYTES||expected%512){why="EXPECTED_SIZE_INVALID";goto done;}
 if(strlen(argv[4])!=64){why="EXPECTED_DIGEST_INVALID";goto done;}
 for(const char *p=argv[4];*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='f'))){why="EXPECTED_DIGEST_INVALID";goto done;}
 rc=3;stage("LSTAT");
 if(lstat(argv[2],&named)){err=errno;why="PATH_STAT_FAILED";goto done;}
 size_before=(long long)named.st_size;
 if(!S_ISREG(named.st_mode)){why="NOT_REGULAR_FILE";goto done;}
 if(named.st_size<0||(uint64_t)named.st_size!=expected){why="SIZE_MISMATCH";goto done;}
 stage("OPEN");fd=open(argv[2],O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0){err=errno;why="OPEN_FAILED";goto done;}
 stage("FSTAT_BEFORE");if(fstat(fd,&before)){err=errno;why="FD_STAT_FAILED";goto done;}
 if(!identity(&named,&before)){why="FILE_CHANGED_BEFORE_READ";goto done;}
 stage("READ");struct sha256 hash;sha256_init(&hash);unsigned char buffer[32768],digest[32];rc=4;
 while(total<expected){
  size_t want=(size_t)((expected-total)<sizeof buffer?(expected-total):sizeof buffer);ssize_t got=read(fd,buffer,want);calls++;
  if(got<0){if(errno==EINTR)continue;err=errno;why="READ_FAILED";goto done;}
  if(!got){why="SHORT_READ";goto done;}
  if((size_t)got>want){why="READ_SIZE_INVALID";goto done;}
  last=(unsigned)got;total+=(unsigned)got;sha256_update(&hash,buffer,(size_t)got);
  if(total>=next||total==expected){printf("progress_bytes=%llu\n",(unsigned long long)total);fflush(stdout);next+=16ull*1024ull*1024ull;}
 }
 ssize_t extra;do {extra=read(fd,buffer,1);calls++;}while(extra<0&&errno==EINTR);
 if(extra<0){err=errno;why="EOF_READ_FAILED";goto done;}
 if(extra){why="FILE_GREW";goto done;}
 sha256_final(&hash,digest);static const char hex[]="0123456789abcdef";
 for(unsigned i=0;i<32;i++){actual[i*2]=hex[digest[i]>>4];actual[i*2+1]=hex[digest[i]&15];}actual[64]=0;
 rc=3;stage("FSTAT_AFTER");if(fstat(fd,&after)){err=errno;why="FINAL_FD_STAT_FAILED";goto done;}size_after=(long long)after.st_size;
 if(!identity(&before,&after)){why="FILE_CHANGED_DURING_READ";goto done;}
 stage("PATH_AFTER");if(lstat(argv[2],&final_name)){err=errno;why="FINAL_PATH_STAT_FAILED";goto done;}
 if(!identity(&after,&final_name)){why="PATH_CHANGED_DURING_READ";goto done;}
 stage("CLOSE");{int closed=close(fd);fd=-1;if(closed){err=errno;why="CLOSE_FAILED";goto done;}}
 stage("DIGEST");if(strcmp(actual,argv[4])){rc=5;why="DIGEST_MISMATCH";goto done;}
 rc=0;why="EXACT_SIZE_AND_DIGEST_VERIFIED";stage("COMPLETE");
 done:
 if(fd>=0&&close(fd)&&!err)err=errno;
 printf("state=%s\nreason=%s\nexit_code=%d\nerrno=%d\nexpected_size=%llu\nsize_before=%lld\nsize_after=%lld\nbytes_read=%llu\nread_calls=%u\nlast_data_read=%u\nactual_sha256=%s\nend=NXL_R11F8_SNAPSHOT_CHECK_END1\n",rc?"REJECTED":"VERIFIED",why,rc,err,(unsigned long long)expected,size_before,size_after,(unsigned long long)total,calls,last,actual);
 return rc;
}
