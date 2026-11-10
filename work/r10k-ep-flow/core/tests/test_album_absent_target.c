#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
static unsigned jpg_creates,renames;static int mode;
static int tracked_open(const char*p,int flags,int permissions){if(strstr(p,".JPG")&&(flags&O_CREAT))jpg_creates++;return open(p,flags,permissions);}
static int absent_rename(const char*a,const char*b){struct stat st;renames++;if(!lstat(b,&st)){errno=EEXIST;return -1;}if(mode==3){errno=EIO;return -1;}return rename(a,b);}
#define open tracked_open
#define rename absent_rename
#ifndef ALBUM_HEADER
#define ALBUM_HEADER "../../common/album_output.h"
#endif
#include ALBUM_HEADER
#undef open
#undef rename
static int put(const char*p,const char*s){int f=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);return f>=0&&write(f,s,strlen(s))==(ssize_t)strlen(s)&&!close(f);}
int main(int argc,char**argv){
 if(argc!=2)return 90;mode=atoi(argv[1]);const char*tmp="/mnt/mmc/DCIM/305PHOTO/NXLI0001.TMP",*jpg="/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG";struct stat st;
 if(!put(tmp,mode==2?"":"COMPLETE"))return 90;if(mode==1&&!put(jpg,"EXISTING"))return 90;
 int ok=ao_commit(tmp,jpg);
 if(!mode){if(!ok||jpg_creates||renames!=1||lstat(jpg,&st)||st.st_size!=8||!lstat(tmp,&st))return 91;}
 else if(mode==1){if(ok||jpg_creates||renames||lstat(jpg,&st)||st.st_size!=8||lstat(tmp,&st)||st.st_size!=8)return 92;}
 else if(mode==2){if(ok||jpg_creates||renames||!lstat(jpg,&st)||lstat(tmp,&st)||st.st_size)return 93;}
 else if(mode==3){if(ok||jpg_creates||renames!=1||!lstat(jpg,&st)||lstat(tmp,&st)||st.st_size!=8)return 94;}
 else return 90;
 puts("PASS no JPG placeholder / no replacement rename; existing target, empty input and failed rename preserved");return 0;
}
