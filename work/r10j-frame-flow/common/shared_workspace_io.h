/* SPDX-License-Identifier: AGPL-3.0-only. Bounded Linux owner/file observations. */
#ifndef NXL_SHARED_WORKSPACE_IO_H
#define NXL_SHARED_WORKSPACE_IO_H
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include "shared_workspace.h"
static int sw_fstat(int fd,struct stat*st){
#ifdef NXL_ORIGINAL_GLIBC_ABI
 /* NX500 libc2.13 exports __fxstat64, not the newer fstat64 alias. Its ARM
  * implementation forwards fd/buffer to fstat64 syscall197; version ignored. */
 _Static_assert(sizeof(struct stat)==104&&offsetof(struct stat,st_mode)==16&&offsetof(struct stat,st_size)==48,"NX500 ARM stat64 syscall ABI");
 extern int __fxstat64(int,int,void*);return __fxstat64(3,fd,st);
#else
 return fstat(fd,st);
#endif
}
static int sw_file_read(const char *path,void *p,size_t bytes){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;if(fd<0)return 0;
 int ok=!sw_fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==(off_t)bytes;size_t at=0;
 while(ok&&at<bytes){ssize_t n=read(fd,(unsigned char*)p+at,bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(close(fd))ok=0;return ok;
}
static int sw_file_new(const char *path,const void *p,size_t bytes){
 int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return 0;size_t at=0;int ok=1;
 while(at<bytes){ssize_t n=write(fd,(const unsigned char*)p+at,bytes-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;return ok;
}
static int sw_owner_ticks(uint32_t pid,uint64_t *ticks){
 char path[80],line[4096];snprintf(path,sizeof path,"/proc/%u/stat",pid);FILE*f=fopen(path,"r");if(!f)return 0;
 int ok=fgets(line,sizeof line,f)!=NULL;if(fclose(f))ok=0;if(!ok)return 0;char*p=strrchr(line,')');if(!p||p[1]!=' ')return 0;p+=2;if(*p=='Z'||*p=='X'||*p=='x')return 0;
 for(unsigned field=3;field<22;field++){p=strchr(p,' ');if(!p)return 0;while(*p==' ')p++;}
 if(*p<'0'||*p>'9')return 0;errno=0;char*end;unsigned long long n=strtoull(p,&end,10);
 if(errno||end==p||(*end!=' '&&*end!='\n')||!n)return 0;*ticks=n;return 1;
}
#include "shared_owner_proof.h"
#endif
