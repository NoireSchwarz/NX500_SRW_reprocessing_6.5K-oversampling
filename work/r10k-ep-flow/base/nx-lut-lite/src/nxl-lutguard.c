/* SPDX-License-Identifier: AGPL-3.0-only. Linux file locks only. */
#define _GNU_SOURCE
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int take(const char*p){
 int f=open(p,O_RDWR|O_CREAT|O_NOFOLLOW,0600);struct stat s;
 if(f<0)return -1;
 if(fstat(f,&s)||!S_ISREG(s.st_mode)||s.st_nlink!=1||flock(f,LOCK_EX|LOCK_NB)){close(f);return -1;}
 return f; /* Deliberately survives exec; all writer descendants share it. */
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_LUT_GUARD1 Linux mode/run locks; no camera access");return 0;}
 if(argc<2||argc>32||argv[1][0]!='/')return 64;
 struct stat s;if(lstat("/tmp/nx-lut-lite",&s)){if(errno!=ENOENT||mkdir("/tmp/nx-lut-lite",0700))return 1;}
 else if(!S_ISDIR(s.st_mode))return 1;
 int a=take("/tmp/nx-lut-lite/video-mode.fd");if(a<0){fputs("ERR VIDEO_MODE_BUSY\n",stderr);return 75;}
 int b=take("/mnt/mmc/NXDEMO51/run.lock");if(b<0){close(a);fputs("ERR SRW_TASK_BUSY\n",stderr);return 75;}
 char*args[35];args[0]="sh";for(int i=1;i<argc;i++)args[i]=argv[i];args[argc]=NULL;
 execv("/bin/sh",args);perror("ERR LUT engine exec");close(b);close(a);return 127;
}
