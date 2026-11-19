#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#ifdef P49_OLD_CONTRACT_PATH
#include P49_OLD_CONTRACT_PATH
#else
#include "../src/param_dma_contract.h"
#endif
#include "../src/gamma_contract.h"
#include "../src/cc_contract.h"
#undef GT_OFFSET
#define GT_OFFSET 0u
#define IQ_FN
static unsigned char area[GT_BYTES],expected[P48_CC_OFFSET+256],source[2048];
static volatile unsigned char*cap_alias=area;
static uint32_t dma_words[64],cc_words[1024],task_words[1024],captured_gamma[512],captured_cc[1024];
static unsigned barriers,checks;
static struct {uint32_t cap,nonce;}ctx;
static struct {const unsigned char*c0;} source_desc;
static const __typeof__(source_desc)*p48_source=&source_desc;
static uint32_t p41_u32(const unsigned char*p,int le){assert(le);uint32_t v;memcpy(&v,p,4);return v;}
static uint32_t p39_fnv(const void*in,unsigned n){const unsigned char*p=in;uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)h=(h^p[i])*16777619u;return h;}
static volatile uint32_t*ptr(uint32_t addr){
 if(addr==ctx.cap+P49_PD_OFFSET)return dma_words;
 if(addr==ctx.cap+P48_CC_OFFSET)return cc_words;
 if(addr==ctx.cap+GT_OFFSET)return (volatile uint32_t*)area;
 assert(addr>=P39_TASK&&addr<P39_TASK+sizeof(task_words)&&!(addr&3u));return task_words+(addr-P39_TASK)/4;
}
static void barrier(void){barriers++;}
#include "../src/gamma_host.inc"
#include "param_dma_producer_fixture.h"
static void read_exact(const char*name,void*out,size_t n){FILE*f=fopen(name,"rb");assert(f&&fread(out,1,n,f)==n&&!fclose(f));}
static void prepare(unsigned mode){
 memset(dma_words,0,sizeof dma_words);memcpy(cc_words,captured_cc,sizeof cc_words);memset(task_words,0,sizeof task_words);
 task_words[0]=0x80592640u;task_words[3364/4]=2;task_words[3388/4]=0x04000001u;
 ctx.cap=0xa39ec000u;ctx.nonce=captured_gamma[2];source_desc.c0=source;
 ((uint32_t*)(expected+P48_CC_OFFSET))[63]=captured_gamma[4];char text[2]={(char)('0'+mode),0};assert(!setenv("NX51_GAMMA_MODE",text,1)&&gt_host_prepare(ctx.nonce));
 uint32_t*q=(uint32_t*)area;assert(gt_request_valid(q,ctx.nonce,captured_gamma[4])&&q[3]==mode);memcpy(q+64,captured_gamma+64,sizeof captured_gamma-256);
}
int main(void){
 read_exact("/c0.bin",source,sizeof source);read_exact("/gamma.bin",captured_gamma,sizeof captured_gamma);read_exact("/cc.bin",captured_cc,sizeof captured_cc);
 assert(captured_gamma[3]==3&&gt_request_valid(captured_gamma,captured_gamma[2],captured_gamma[4])&&gt_audit_valid(captured_gamma,captured_gamma[2],captured_gamma[4]));
 uint32_t reference[64];
 for(unsigned mode=0;mode<=GT_MODE_MAX;mode++){
  prepare(mode);int got=p49_pd_record_single();
#ifdef P49_EXPECT_OLD
  assert(got==(mode<=2));
#else
  assert(got==1);
#endif
  assert(dma_words[52]==mode);if(!mode)memcpy(reference,dma_words,sizeof reference);else for(unsigned i=0;i<64;i++)if(i!=52)assert(reference[i]==dma_words[i]);
  assert(p49_pd_single_audit_valid(dma_words,ctx.nonce)==got);assert(!p49_pd_factory_audit_valid(dma_words,ctx.nonce)&&!p49_pd_audit_valid(dma_words,ctx.nonce));checks+=4;
 }
#ifndef P49_EXPECT_OLD
 for(unsigned fault=0;fault<14;fault++){
  prepare(3);
  switch(fault){case 0:dma_words[P49_PD_W_ACTIVE]=1;break;case 1:dma_words[P49_PD_W_INFLIGHT]=1;break;case 2:cc_words[71]=1;break;case 3:task_words[0]^=1;break;case 4:task_words[3364/4]=1;break;case 5:task_words[3388/4]^=1;break;case 6:cc_words[3]=0;break;case 7:gt()[GT_BEFORE_LOAD]=0;break;case 8:gt()[GT_CONFIG_LOADS]=1;break;case 9:gt()[GT_CONFIG_RETURN]=0;break;case 10:gt()[GT_REFRESH_RETURN]=0;break;case 11:gt()[GT_FINAL_LOAD_STATUS]=1;break;case 12:ctx.nonce=0;break;case 13:gt()[3]=6;break;}
  assert(!p49_pd_record_single());checks++;
 }
 for(unsigned mode=0;mode<=GT_MODE_MAX;mode++){
  prepare(mode);assert(p49_pd_record_single());uint32_t saved[64];memcpy(saved,dma_words,sizeof saved);
  for(unsigned i=0;i<64;i++){dma_words[i]=(i==52?6u:dma_words[i]^1u);assert(!p49_pd_single_audit_valid(dma_words,ctx.nonce));memcpy(dma_words,saved,sizeof saved);checks++;}
  assert(!p49_pd_single_audit_valid(dma_words,0)&&!p49_pd_single_audit_valid(dma_words,ctx.nonce^1u));checks+=2;
 }
 printf("PASS %u gate checks: actual host+RTOS producer, modes0-5, 14 producer faults, all64 audit words, nonce and DMA guards.\n",checks);
#else
 puts("PASS reproduced OLD phase143 rejection of LINEAR3/LOG4; modes0-2 accepted with both actual camera failure captures.");
#endif
 return 0;
}
