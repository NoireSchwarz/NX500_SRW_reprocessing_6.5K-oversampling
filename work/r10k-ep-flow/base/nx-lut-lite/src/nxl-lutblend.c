/* SPDX-License-Identifier: AGPL-3.0-only.
 * metol M3 dual-strength semantics, two percentage-rounding steps as in
 * WApaperplane/metol nxui.c + lut.sh blend (2026-11-02).
 * Bounded regular-file arithmetic only. No camera/device access or reload.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#define SLOT 19712u
#define TABLE 19652u
static unsigned char base[SLOT],a[SLOT],b[SLOT],out[SLOT];
#include "lut_parameters.h"
static int read_table(const char*path,unsigned char*p){
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return 0;struct stat st;
 int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size==SLOT;size_t at=0;
 while(ok&&at<SLOT){ssize_t n=read(fd,p+at,SLOT-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(size_t)n;}
 if(close(fd))ok=0;if(!ok)return 0;
 for(unsigned i=3;i<TABLE;i+=4)if(p[i])return 0;
 for(unsigned i=0;i<12;i++)if(p[TABLE+i]!=p[TABLE-4+(i&3)])return 0;
 for(unsigned i=TABLE+12;i<SLOT;i++)if(p[i])return 0;return 1;
}
static int parameters(char**v,struct lp_params*p){
 int*out=&p->strength;for(unsigned i=0;i<4;i++){char*end;size_t n=strlen(v[i]);if(!n||n>3)return 0;long x=strtol(v[i],&end,10);if(*end||x< -10||x>100)return 0;out[i]=(int)x;}return lp_valid(p);
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXLUTBLEND2 graded A/B metol M3; fixed native CrCbY; spatial sharpness plan only");return 0;}
 struct lp_params pa,pb;
 if(argc!=14||!parameters(argv+4,&pa)||!parameters(argv+8,&pb)||strcmp(argv[12],"--output")){fputs("ERR usage: blend BASE A B A-strength A-hue A-sharp A-contrast B-strength B-hue B-sharp B-contrast --output NEW_FILE\n",stderr);return 2;}
 if(!read_table(argv[1],base)||!read_table(argv[2],a)||!read_table(argv[3],b)){fputs("ERR source table shape or extent\n",stderr);return 3;}
 for(unsigned i=0;i<TABLE;i+=4)lp_pixel(base+i,a+i,b+i,&pa,&pb,out+i);
 for(unsigned i=0;i<12;i++)out[TABLE+i]=out[TABLE-4+(i&3)];
 int fd=open(argv[13],O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0){perror("ERR output");return 4;}unsigned at=0;int ok=1;
 while(at<SLOT){ssize_t n=write(fd,out+at,SLOT-at);if(n<0&&errno==EINTR)continue;if(n<=0){ok=0;break;}at+=(unsigned)n;}
 if(ok&&fsync(fd))ok=0;if(close(fd))ok=0;if(!ok){fputs("ERR output incomplete\n",stderr);return 5;}
 printf("NXLUTBLEND2 bytes=%u sharp_offset=%d\n",SLOT,lp_sharp(&pa,&pb));return 0;
}
