#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
static int reservation=-1,live,closed,renames,mode;
static int checked_open(const char*p,int flags,int permissions){int f=open(p,flags,permissions);if(f>=0&&(flags&O_EXCL)){reservation=f;live=1;closed=0;}return f;}
static int checked_close(int f){int rc=close(f);if(f==reservation){live=0;closed=1;if(mode==1){errno=EIO;return -1;}if(mode==2){int changed=open("/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG",O_WRONLY|O_TRUNC);if(changed>=0){write(changed,"FOREIGN",7);close(changed);}}}return rc;}
static int checked_rename(const char*a,const char*b){renames++;if(live||!closed){errno=EBUSY;return -1;}return rename(a,b);}
#define open checked_open
#define close checked_close
#define rename checked_rename
#ifndef ALBUM_HEADER
#define ALBUM_HEADER "../../common/album_output.h"
#endif
#include ALBUM_HEADER
#undef open
#undef close
#undef rename
int main(int argc,char**argv){
 if(argc!=2)return 90;mode=atoi(argv[1]);const char*tmp="/mnt/mmc/DCIM/305PHOTO/NXLI0001.TMP",*jpg="/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG";
 int fd=open(tmp,O_WRONLY|O_CREAT|O_EXCL,0600);if(fd<0||write(fd,"COMPLETE",8)!=8||close(fd))return 90;
 int rc=ao_commit(tmp,jpg);struct stat st;
 if(!mode){if(!rc||live||!closed||renames!=1||lstat(jpg,&st)||st.st_size!=8||!lstat(tmp,&st))return 91;}
 else if(mode==1){if(rc||live||renames||!lstat(jpg,&st)||lstat(tmp,&st)||st.st_size!=8)return 92;}
 else if(mode==2){if(rc||live||renames||lstat(jpg,&st)||st.st_size!=7||lstat(tmp,&st)||st.st_size!=8)return 93;}
 else return 90;
 puts("PASS exFAT destination closed before replacement; close error and changed reservation preserve complete TMP / foreign file");return 0;
}
