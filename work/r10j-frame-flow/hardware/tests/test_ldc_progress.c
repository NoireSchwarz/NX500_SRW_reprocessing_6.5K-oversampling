#define _GNU_SOURCE
#define NXL_FAST_PIPELINE
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "../src/bitb_job.h"
#include "../../common/ldc_observation.h"
static struct sw_lease full_workspace_original;
static struct {unsigned stop_verified;} ldc_audit;
struct native_context {volatile unsigned char*memory;volatile uint32_t*top,*ldc,*rtos;unsigned origin,ldc_bytes,last_errno,last_wait_errno,last_ioctl;int last_return;FILE*log;};
static unsigned char memory[SW_BYTES];
static uint32_t top[2048],ldc[1024],rtos[1024],now=1000;
static void barrier(void*v){(void)v;}
static uint32_t millis(void*v){(void)v;return now;}
static int kernel_read(void*v,uint32_t p,void*out,unsigned n){(void)v;(void)p;memset(out,0,n);return 1;}
static int ctl(void*v,uint32_t cmd,void*p){struct native_context*c=v;(void)cmd;(void)p;c->last_errno=EIO;return -1;}
#include "../src/ldc_progress.inc"
int main(void){
 struct native_context c={.memory=memory,.top=top,.ldc=ldc,.rtos=rtos,.origin=1000,.ldc_bytes=4096};full_workspace_original.nonce=0x12345678;memset(full_workspace_original.source_sha256,'a',64);
 memory[SW_INPUT]=0xa9;memory[SW_TABLE]=0xb3;memory[SW_LEASE]=0xc2;
 for(unsigned i=0;i<LDP_POINTS;i++){
  assert(ldp_offset(i)>=SW_LDC_OUT&&ldp_offset(i)+8<SW_TABLE);
  for(unsigned j=0;j<i;j++)assert(ldp_offset(i)!=ldp_offset(j));
 }
 ldc_progress_capture(&c,0);assert(ldp.seeded&&ldp.count==1&&!ldp.records[0].changed_points&&!ldc_progress_coverage(&c));
 assert(memory[SW_INPUT]==0xa9&&memory[SW_TABLE]==0xb3&&memory[SW_LEASE]==0xc2);
 for(unsigned j=0;j<8;j++)memory[ldp_offset(0)+j]^=1;
 ldc_progress_capture(&c,4);assert(ldp.records[1].changed_points==1&&ldp.records[1].changed_bytes==8&&!ldc_progress_coverage(&c));
 for(unsigned i=1;i<LDP_POINTS;i++)for(unsigned j=0;j<8;j++)memory[ldp_offset(i)+j]^=1;
 ldc_progress_capture(&c,4);assert(ldc_progress_coverage(&c)&&ldp.records[2].changed_points==256);
 ldc_progress_capture(&c,0);for(unsigned i=0;i<LDP_POINTS;i++){unsigned k=i%128;if(ldp_cols[k%16]<480)for(unsigned j=0;j<8;j++)memory[ldp_offset(i)+j]^=1;}
 ldc_progress_capture(&c,4);assert(!ldc_progress_coverage(&c)&&ldp.records[1].changed_points==112);
 /* Pixel changes alone cannot set a hardware completion: no such writes. */
 assert(!top[0x1070/4]&&!top[0x10c8/4]);
 memset(ldp.records,255,sizeof ldp.records);ldp.count=LDP_MAX;ldp.overflow=0;
 for(unsigned i=0;i<LDP_MAX;i++)for(unsigned j=0;j<LDP_POINTS;j++)ldp.records[i].changed[j]=255;
 full_workspace_original.nonce=~0u;
 struct bitb_request q={.pa=~0u,.nonce=123,.crc=~0u};struct bitb_result r={.fresh_completion=0,.wait_return=-1,.wait_errno=110};
 assert(ldc_progress_file("/mnt/mmc",&q,&r));
 ldc_progress_capture(&c,4);assert(ldp.count==LDP_MAX&&ldp.overflow);
 puts("PASS production256 marker points; source/control untouched; partial/full coverage; bounded maximum serializer; no completion from pixels");return 0;
}
