#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../common/album_output.h"
static void put(const char*p,const char*text){int f=open(p,O_WRONLY|O_CREAT|O_EXCL,0600);assert(f>=0&&write(f,text,strlen(text))==(ssize_t)strlen(text)&&!close(f));}
static void content(const char*p,const char*text){char b[64]={0};int f=open(p,O_RDONLY);assert(f>=0&&read(f,b,sizeof b)==(ssize_t)strlen(text)&&!strcmp(b,text)&&!close(f));}
int main(void){
 const char*source="/mnt/mmc/DCIM/305PHOTO/SAM_0383.SRW";
 assert(ao_target("/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG",0)&&ao_target("/mnt/mmc/DCIM/305PHOTO/NXLI9999.TMP",1));
 const char*bad[]={"/mnt/mmc/DCIM/../NXLI0001.JPG","/mnt/mmc/DCIM/305PHOTO/sub/NXLI0001.JPG","/mnt/mmc/DCIM/305PHOTO/NXLI0000.JPG","/mnt/mmc/DCIM/305PHOTO/SAM_0001.JPG","/mnt/mmc/DCIM/305PHOTO/NXLI0001.jpg","/mnt/mmc/NXLI0001.JPG"};
 for(unsigned i=0;i<sizeof bad/sizeof*bad;i++)assert(!ao_target(bad[i],0));
 assert(ao_prepare(source));assert(!strcmp(getenv("NX51_FINAL_JPEG"),"/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG"));
 put("/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG","EXISTING");put("/mnt/mmc/DCIM/305PHOTO/NXLI0002.TMP","PENDING_OTHER");assert(ao_prepare(source));
 assert(!strcmp(getenv("NX51_FINAL_JPEG"),"/mnt/mmc/DCIM/305PHOTO/NXLI0003.JPG"));
 char pending[600],final[600];strcpy(pending,getenv("NX51_FINAL_PENDING"));strcpy(final,getenv("NX51_FINAL_JPEG"));
 put(pending,"COMPLETE_JPEG_BYTES");assert(!ao_commit(pending,"/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG"));content("/mnt/mmc/DCIM/305PHOTO/NXLI0001.JPG","EXISTING");
 struct stat missing;assert(ao_commit(pending,final));content(final,"COMPLETE_JPEG_BYTES");assert(lstat(pending,&missing));assert(!ao_commit(pending,final));content(final,"COMPLETE_JPEG_BYTES");
 assert(!ao_commit("/mnt/mmc/DCIM/305PHOTO/NXLI0004.TMP","/mnt/mmc/DCIM/305PHOTO/NXLI0004.JPG"));assert(lstat("/mnt/mmc/DCIM/305PHOTO/NXLI0004.JPG",&missing));
 assert(!ao_prepare("/mnt/mmc/DCIM/305PHOTO/../SAM_0383.SRW")&&!ao_prepare("/mnt/mmc/DCIM/305PHOTO/SAM_0383.JPG"));
 content(source,"SOURCE_SRW");puts("PASS same-album DCF names, repeat numbering, existing/pending collision preservation, complete exclusive commit and failed-commit reservation cleanup");return 0;
}
