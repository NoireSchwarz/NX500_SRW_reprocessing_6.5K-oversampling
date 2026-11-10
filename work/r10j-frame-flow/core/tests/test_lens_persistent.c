#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <assert.h>
static unsigned char ram[0x2000];
static uint32_t fail_at,mutate_at;static unsigned body_reads,frames;static uint64_t clock_now;
static FILE*context_file,*events;
static uint64_t now(void){return ++clock_now;}
static int block(uint32_t at,uint32_t*out,unsigned words){
 assert(at>=0x8142e000u&&(uint64_t)at+words*4<=0x81430000u&&words<=282);
 if(at==fail_at)return 0;
 memcpy(out,ram+at-0x8142e000u,words*4);
 if(words>1)body_reads++;
 if(at==mutate_at&&words>1){ram[0x1430]^=1;mutate_at=0;}
 return 1;
}
static int frame(FILE*f,unsigned kind,unsigned seq,uint64_t begin,uint64_t end,const void*data,unsigned bytes){
 (void)f;(void)seq;(void)begin;(void)end;(void)data;assert(kind==10&&bytes==1452);frames++;return 1;
}
#include "../../lens/lens_persistent_capture.inc"
static void word(unsigned off,uint32_t v){memcpy(ram+off,&v,4);}
static void init(void){
 memset(ram,0,sizeof ram);fail_at=mutate_at=body_reads=0;
 FILE*f=fopen("/source.bin","rb");assert(f&&fread(ram+0xea4,1,272,f)==272&&!fclose(f));
 f=fopen("/storage.bin","rb");assert(f&&fread(ram+0xfc8,1,1128,f)==1128&&!fclose(f));
 word(0x1430,0x80582d10);word(0x1450,0x80582da8);
}
int main(void){
 struct lpr_sample s;assert(sizeof s==1452&&offsetof(struct lpr_sample,source)==52&&offsetof(struct lpr_sample,storage)==324);
 init();assert(lens_persistent_read(&s)==1&&s.status==1&&s.available_mask==3&&body_reads==2);
 assert(!memcmp(s.source,ram+0xea4,272)&&!memcmp(s.storage,ram+0xfc8,1128));
 /* No EP roots exist in this fixture. Retained packets remain observable. */
 init();word(0x1430,0);assert(lens_persistent_read(&s)==1&&s.status==2&&!body_reads);
 init();word(0xea4,0);assert(lens_persistent_read(&s)==1&&s.status==2&&!body_reads);
 init();fail_at=LPR_CTRL0;assert(lens_persistent_read(&s)==-1&&s.status==3&&!body_reads);
 init();fail_at=LPR_INPUT;assert(lens_persistent_read(&s)==-1&&s.status==3&&!body_reads);
 init();fail_at=LPR_STORAGE;assert(lens_persistent_read(&s)==-1&&s.status==3&&s.available_mask==1);
 init();mutate_at=LPR_STORAGE;assert(lens_persistent_read(&s)==0&&s.status==4);
 init();lens_persistent_sample(1);lens_persistent_sample(2);assert(frames==1);
 clock_now+=1000000000ULL;lens_persistent_sample(3);assert(frames==2);
 ram[0xf00]^=1;lens_persistent_sample(4);assert(frames==3);
 puts("PASS retained LDC body, type/read/change gates, active-EP independence, deduplication and heartbeat; no hardware");return 0;
}
