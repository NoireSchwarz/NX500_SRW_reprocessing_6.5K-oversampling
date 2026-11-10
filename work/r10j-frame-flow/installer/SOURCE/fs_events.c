#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static void event(const char*fn,const char*p,int rc,int saved){
 const char*log=getenv("NXL_FS_EVENTS");if(!log||!strstr(p,"nx-lut"))return;
 struct stat st;int exists=lstat(p,&st)==0;struct timespec t;clock_gettime(CLOCK_REALTIME,&t);
 char b[4096];int n=snprintf(b,sizeof b,"%ld.%09ld pid=%ld ppid=%ld %s path=%s rc=%d errno=%d exists=%d inode=%llu\n",(long)t.tv_sec,t.tv_nsec,(long)getpid(),(long)getppid(),fn,p,rc,saved,exists,(unsigned long long)(exists?st.st_ino:0));
 int fd=open(log,O_WRONLY|O_CREAT|O_APPEND,0600);if(fd>=0){if(n>0&&n<(int)sizeof b){ssize_t wrote=write(fd,b,n);(void)wrote;}close(fd);}errno=saved;
}
int mkdir(const char*p,mode_t mode){static int(*fn)(const char*,mode_t);if(!fn)fn=dlsym(RTLD_NEXT,"mkdir");int r=fn(p,mode),e=errno;event("mkdir",p,r,e);return r;}
int rmdir(const char*p){static int(*fn)(const char*);if(!fn)fn=dlsym(RTLD_NEXT,"rmdir");int r=fn(p),e=errno;event("rmdir",p,r,e);return r;}
int mkdirat(int d,const char*p,mode_t mode){static int(*fn)(int,const char*,mode_t);if(!fn)fn=dlsym(RTLD_NEXT,"mkdirat");int r=fn(d,p,mode),e=errno;event("mkdirat",p,r,e);return r;}
int unlinkat(int d,const char*p,int flags){static int(*fn)(int,const char*,int);if(!fn)fn=dlsym(RTLD_NEXT,"unlinkat");int r=fn(d,p,flags),e=errno;event(flags&AT_REMOVEDIR?"unlinkat_dir":"unlinkat_file",p,r,e);return r;}
