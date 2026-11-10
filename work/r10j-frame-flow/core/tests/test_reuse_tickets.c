#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include "../../common/source_ticket.h"
#include "../../common/jpeg_ticket.h"
static void replace(const char*p,const void*b,unsigned n){int f=open(p,O_WRONLY|O_TRUNC);assert(f>=0&&write(f,b,n)==(ssize_t)n&&!close(f));}
int main(void){
 const char*op="0123456789abcdef0123456789abcdef",*source="/mnt/mmc/DCIM/305PHOTO/SAM_0383.SRW";
 const char*path="/tmp/nx-lut-lite/source-ticket-0123456789abcdef0123456789abcdef-123.bin";char hash[68]={0},other[68]={0},out[65];memset(hash,'a',64);memset(other,'b',64);
 struct stat s={0};s.st_dev=1;s.st_ino=2;s.st_mode=S_IFREG|0600;s.st_size=40000000;s.st_mtim.tv_sec=1700000000;s.st_ctim.tv_sec=1700000001;s.st_mtim.tv_nsec=99;s.st_ctim.tv_nsec=100;
 assert(stkt_save(path,op,source,hash,&s)&&!stkt_save(path,op,source,hash,&s));assert(stkt_load(path,op,source,hash,&s,out)&&!strcmp(out,hash));
 assert(!stkt_load(path,op,source,other,&s,out));assert(!stkt_load(path,"1123456789abcdef0123456789abcdef",source,hash,&s,out));assert(!stkt_load(path,op,"/mnt/mmc/DCIM/305PHOTO/SAM_0382.SRW",hash,&s,out));
 struct stat changed=s;changed.st_ino++;assert(!stkt_load(path,op,source,hash,&changed,out));changed=s;changed.st_size++;assert(!stkt_load(path,op,source,hash,&changed,out));changed=s;changed.st_mtim.tv_nsec++;assert(!stkt_load(path,op,source,hash,&changed,out));changed=s;changed.st_ctim.tv_sec++;assert(!stkt_load(path,op,source,hash,&changed,out));
 struct stkt_ticket source_ticket;int f=open(path,O_RDONLY);assert(f>=0&&read(f,&source_ticket,sizeof source_ticket)==sizeof source_ticket&&!close(f));
 const unsigned positions[]={0,4,8,12,16,24,32,40,48,56,60,64,575,576,640,644,676,680,1019,1020};
 for(unsigned i=0;i<sizeof positions/sizeof*positions;i++){struct stkt_ticket bad=source_ticket;((unsigned char*)&bad)[positions[i]]^=1;replace(path,&bad,sizeof bad);assert(!stkt_load(path,op,source,hash,&s,out));}
 replace(path,&source_ticket,sizeof source_ticket);assert(stkt_load(path,op,source,hash,&s,out));replace(path,&source_ticket,sizeof source_ticket-1);assert(!stkt_load(path,op,source,hash,&s,out));
 assert(!stkt_path("/mnt/mmc/ticket.bin",op)&&!stkt_path("/tmp/nx-lut-lite/source-ticket-0123456789abcdef0123456789abcdef-../123.bin",op));
 const char*jp="/tmp/nx-lut-lite/jpeg-ticket.bin";struct jt_ticket ticket;s.st_size=10000000;
 assert(jt_write(jp,&s,777,0x98000000,123,hash,other)&&!jt_write(jp,&s,777,0x98000000,123,hash,other)&&jt_read(jp,&ticket)&&jt_matches(&ticket,&s,777,0x98000000,123,hash,other));
 unsigned rejected=0;for(unsigned i=0;i<sizeof ticket;i++){struct jt_ticket bad=ticket;((unsigned char*)&bad)[i]^=1;assert(!jt_matches(&bad,&s,777,0x98000000,123,hash,other));rejected++;}
 assert(!jt_matches(&ticket,&s,778,0x98000000,123,hash,other)&&!jt_matches(&ticket,&s,777,0x98000004,123,hash,other)&&!jt_matches(&ticket,&s,777,0x98000000,124,hash,other)&&!jt_matches(&ticket,&s,777,0x98000000,123,other,other));
 changed=s;changed.st_ctim.tv_nsec++;assert(!jt_matches(&ticket,&changed,777,0x98000000,123,hash,other));changed=s;changed.st_mode=S_IFLNK;assert(!jt_matches(&ticket,&changed,777,0x98000000,123,hash,other));
 printf("PASS source identity/operation/path/hash/stat/truncation cases; JPEG %u single-byte corruption cases and ownership/source/stat binding\n",rejected);return 0;
}
