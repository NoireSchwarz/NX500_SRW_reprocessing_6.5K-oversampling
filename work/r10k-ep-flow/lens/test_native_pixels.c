/* Exercise the actual passive capture logic. All reads are synthetic. */
#define _GNU_SOURCE
#define pread modeled_pread
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "passive_profile/combined_core.h"
static int dev=99;static FILE*context_file;
static uint32_t dma[4],image[44],state_word=3;static unsigned reads,records,torn;
static unsigned char recorded[16384];static unsigned recorded_bytes;
static int mapped_read(uint32_t at,void*p,unsigned n,unsigned guarded){(void)guarded;if(at==0x817d7b90u){assert(n==16);memcpy(p,dma,n);return 1;}assert(at==0x8247e800u&&n==176);memcpy(p,image,n);if(torn)((uint32_t*)p)[3]^=1;return 1;}
static int word(uint32_t at,uint32_t*v){if(at==P34_TASK+3364u)*v=state_word;else if(at==P35_EP+3364u)*v=3;else if(at==P35_EP+3444u)*v=P35_STILL;else{assert(at==P35_EP+3412u);*v=0x82400000;}return 1;}
static uint64_t now(void){return 1000000000u;}
static int frame(FILE*f,unsigned kind,unsigned seq,uint64_t b,uint64_t e,const void*p,unsigned n){(void)f;(void)seq;(void)b;(void)e;assert(kind==13&&n<sizeof recorded);memcpy(recorded,p,n);recorded_bytes=n;records++;return 1;}
ssize_t modeled_pread(int fd,void*p,size_t n,off_t at){assert(fd==99&&at>=0x94000000&&at+n<0xbb6cc000&&n==16);for(unsigned i=0;i<n;i++)((unsigned char*)p)[i]=(unsigned char)((uint64_t)at+i);reads++;return n;}
#include "native-pixel-capture.inc"
int main(void){
 struct p36_snapshot s={0};s.base.w[1]=3;s.base.w[P35_EP_BASE+54]=1;s.base.w[P35_EP_BASE+30]=1;s.base.w[P35_EP_BASE+1]=3;
 uint32_t*b=s.base.w+P35_BLOCK_BASE;b[0]=0x82400000;b[2]=0x80582da8;b[3]=1;b[5]=0x80713a28;b[19]=1;b[20]=0x8247e800;
 image[0]=0x80588910;image[3]=6480;image[4]=4320;memcpy(b+21,image,sizeof image);
 native_pixel_sample(1,&s);assert(records==1&&native_pixels.sample.status==PS_OTF_NO_RAM&&!reads&&!ps_valid(&native_pixels.sample)&&recorded_bytes==13392);
 native_pixel_sample(2,&s);assert(records==1);s.base.w[1]=state_word=7;native_pixel_sample(3,&s);assert(records==1);
 s.base.w[1]=state_word=3;dma[0]=image[27]=0x98000000;dma[1]=image[29]=dma[0]+0x1ad0000;dma[2]=6496;dma[3]=0x41;image[36]=2u*6496u*4320u;memcpy(b+21,image,sizeof image);
 native_pixel_sample(4,&s);assert(records==2&&native_pixels.sample.status==PS_NOT_RETIRED&&!reads);
 s.base.w[1]=state_word=7;native_pixel_sample(5,&s);assert(records==3&&reads==1600&&ps_valid(&native_pixels.sample));
 torn=1;native_pixel_sample(6,&s);assert(records==4&&native_pixels.sample.status==PS_CHANGED&&!ps_valid(&native_pixels.sample));
 torn=0;dma[3]=0x42;native_pixel_sample(7,&s);assert(records==5&&native_pixels.sample.status==PS_UNSUPPORTED_LAYOUT&&reads==3200);
 puts("PASS native OTF exclusion, inactive/format gates, bounded double-read RAM samples, deduplication and changed-header rejection; no hardware");return 0;
}
