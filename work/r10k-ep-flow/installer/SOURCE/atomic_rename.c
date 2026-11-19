/* NXL_ATOMIC_RENAME1: one checked same-directory rename, never unlink/copy. */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
static int path_ok(const char *s){
 size_t n=strlen(s);return n>1&&n<1024&&s[0]=='/'&&!strstr(s,"/../")&&!strstr(s,"/./")&&!strstr(s,"//");
}
int main(int argc,char **v){
 if(argc==2&&!strcmp(v[1],"--version")){puts("NXL_ATOMIC_RENAME1");return 0;}
 if(argc!=3||!path_ok(v[1])||!path_ok(v[2])){fputs("NXL_ATOMIC_RENAME refused stage=ARGUMENTS\n",stderr);return 64;}
 const char *src=v[1],*dst=v[2],*sb=strrchr(src,'/'),*db=strrchr(dst,'/');
 const char prefix[]="libmmfcore.so.nxl-";
 if(!sb||!db||(size_t)(sb-src)!=(size_t)(db-dst)||memcmp(src,dst,(size_t)(sb-src))||strcmp(db+1,"libmmfcore.so")||strncmp(sb+1,prefix,sizeof(prefix)-1)||
    (strncmp(sb+sizeof(prefix),"new.",4)&&strncmp(sb+sizeof(prefix),"restore.",8))){
  fputs("NXL_ATOMIC_RENAME refused stage=PATH_SCOPE\n",stderr);return 65;
 }
 struct stat s,d,after;
 if(lstat(src,&s)||lstat(dst,&d)){fprintf(stderr,"NXL_ATOMIC_RENAME refused stage=LSTAT errno=%d\n",errno);return 66;}
 if(!S_ISREG(s.st_mode)||!S_ISREG(d.st_mode)||s.st_dev!=d.st_dev||s.st_ino==d.st_ino||s.st_nlink<2||d.st_nlink<2){
  fputs("NXL_ATOMIC_RENAME refused stage=INODE_GUARD\n",stderr);return 66;
 }
 printf("NXL_ATOMIC_RENAME begin src=%s dst=%s src_inode=%llu src_links=%lu dst_inode=%llu dst_links=%lu\n",src,dst,(unsigned long long)s.st_ino,(unsigned long)s.st_nlink,(unsigned long long)d.st_ino,(unsigned long)d.st_nlink);
 fflush(stdout);
 /* No remove/unlink, no EXDEV copy fallback, and no shell command. */
 if(rename(src,dst)){int e=errno;fprintf(stderr,"NXL_ATOMIC_RENAME failed stage=RENAME errno=%d message=%s\n",e,strerror(e));return 67;}
 if(lstat(dst,&after)||after.st_dev!=s.st_dev||after.st_ino!=s.st_ino||after.st_nlink<2){fputs("NXL_ATOMIC_RENAME failed stage=READBACK\n",stderr);return 68;}
 if(lstat(src,&after)==0||errno!=ENOENT){fputs("NXL_ATOMIC_RENAME failed stage=SOURCE_REMAINS\n",stderr);return 68;}
 puts("NXL_ATOMIC_RENAME committed single_rename=1 pre_unlink=0");return 0;
}
