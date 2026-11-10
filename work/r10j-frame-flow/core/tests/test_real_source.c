#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
static volatile sig_atomic_t stop_requested;
static unsigned ms(void){return 0;}
#include "bayer_front.h"
#include "handoff_core.h"
#include "srw_iq.h"
static FILE *out;
static int emit(void *unused,uint32_t y,const unsigned char *p,size_t n){(void)unused;assert(y<P31_STAGE_HEIGHT&&n==P31_STAGE_STRIDE);return fwrite(p,1,n,out)==n;}
int main(int argc,char **argv){
 assert(argc==5);char path[1200];snprintf(path,sizeof path,"%s/decode.log",argv[3]);FILE *log=fopen(path,"w");assert(log);struct p32_front f;
 assert(p32_front_open(&f,argv[1],argv[2],argv[3],log));out=fopen(argv[4],"wb");assert(out);
 uint32_t iq[256];assert(p41_srw_info(f.source,iq)&&iq[4]==160&&iq[8]==364&&iq[11]==613);
 assert(p32_front_write(&f,NULL,emit));assert(!fclose(out));assert(f.matched==4336&&f.stored==4384&&f.verified);
 assert(!strcmp(f.raw_hash,P32_BAYER_HASH)&&!strcmp(f.layout_hash,P31_STAGE_HASH));assert(p32_front_close(&f));fclose(log);
 puts("PASS production streaming decoder:4336 exact real rows and4384 staged rows, fixed hashes");return 0;
}
