/* Read-only hardware observer handoff. Acknowledgement is published only
 * after its EP MMIO mappings are gone. This does not retire camera DMA. */
#ifndef NXL_LENS_HW_YIELD_H
#define NXL_LENS_HW_YIELD_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int lhy_token(const char*t){
 if(strlen(t)!=32)return 0;for(unsigned i=0;i<32;i++)if(!((t[i]>='0'&&t[i]<='9')||(t[i]>='a'&&t[i]<='f')))return 0;return 1;
}
static int lhy_read(int dir,const char*name,char token[33]){
 int f=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0)return 0;struct stat st;char b[34];unsigned n=0;
 int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size==33;
 while(ok&&n<33){ssize_t z=read(f,b+n,33-n);if(z<0&&errno==EINTR)continue;if(z<=0){ok=0;break;}n+=(unsigned)z;}
 if(close(f))ok=0;if(!ok||n!=33||b[32]!='\n')return 0;b[32]=0;if(!lhy_token(b))return 0;memcpy(token,b,33);return 1;
}
static int lhy_publish(int dir,const char*name,const char*token){
 if(!lhy_token(token))return 0;char temporary[100],b[34];int z=snprintf(temporary,sizeof temporary,".%s.%s.%u",name,token,(unsigned)getpid());if(z<=0||z>=(int)sizeof temporary)return 0;
 int f=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return 0;memcpy(b,token,32);b[32]='\n';unsigned n=0;int ok=1;
 while(n<33){ssize_t count=write(f,b+n,33-n);if(count<0&&errno==EINTR)continue;if(count<=0){ok=0;break;}n+=(unsigned)count;}
 if(fsync(f))ok=0;if(close(f))ok=0;
 if(ok&&renameat(dir,temporary,dir,name))ok=0;if(!ok){unlinkat(dir,temporary,0);return 0;}return !fsync(dir);
}
#endif
