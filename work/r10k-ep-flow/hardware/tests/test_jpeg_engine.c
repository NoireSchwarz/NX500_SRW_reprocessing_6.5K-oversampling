/* SPDX-License-Identifier: AGPL-3.0-only
 * Actual shared engine and validator, modeled MMIO/IRQ/address boundaries.
 * Fixture is a software JPEG, not evidence of native hardware output.
 */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "bitb_job.h"
#include "jpeg_engine.h"
#include "pipeline_jpeg_fixtures.h"
#define fixture fixture_reference
static struct model {uint32_t regs[2][2048],mutex,mask,selector,error,queue5;unsigned mode,resets,jobs,locked,source,preserve;unsigned char memory[BITB_BYTES];} c;
static struct jpeg_snapshot s;
static uint32_t rd(void *p,unsigned b,uint32_t a){(void)p;return c.regs[b][a/4];}
static void wr(void *p,unsigned b,uint32_t a,uint32_t v){(void)p;if(c.mode==12&&b==0&&a==0x1020&&c.resets==2)v^=0x80;c.regs[b][a/4]=v;if(b==0&&a==0x1060)c.regs[0][0x10d8/4]=0;}
static uint32_t kw(void *p,uint32_t a){(void)p;return a==0xc07555c4?c.selector:c.error;}
static int state(void *p,unsigned locked){(void)p;if(c.mode==1)return 0;if(c.mode==4&&!c.jobs)return 0;return c.locked==(locked!=0)&&c.mask==(locked==2?0x24u:0)&&!c.queue5&&c.selector==3&&!c.error;}
static int idle(void *p){(void)p;return 1;}
static void barrier(void *p){(void)p;}
static void note(void *p,unsigned n){(void)p;(void)n;}
static int ctl(void *p,uint32_t cmd,void *arg){
 (void)p;uint32_t v=arg?*(uint32_t*)arg:0;
 switch(cmd){
 case 0x6828:assert(!c.locked);c.locked=1;return 0;
 case 0x6829:assert(c.locked);c.locked=0;return 0;
 case 0x400c6809:c.queue5=0;return 0;
 case 0x40046810:assert(v==2||v==5);c.mask|=1u<<v;c.regs[0][0x1080/4]=c.mask;return 0;
 case 0x40046811:if(c.mode==13&&v==5)return -1;c.mask&=~(1u<<v);c.regs[0][0x1080/4]=c.mask;return 0;
 case 0xc00c6808:
  assert(c.jobs==1&&c.mask==0x24&&c.queue5==0);
  if(c.mode==5)return -1;
  if(c.mode==6)c.error=1;
  if(c.mode==18)c.queue5=1;
  return 900;
 case 0xc0046816:if(c.mode==15)return -1;c.selector=v?3:5;c.error=0;return 0;
 default:assert(0);return -1;
 }
}
static int call(void *p,enum jpeg_call fn,uintptr_t a,uint32_t b){
 (void)p;(void)b;
 switch(fn){
 case J_CLOCK:c.regs[0][0x1020/4]&=~0x80;c.regs[0][0x1024/4]&=~0x1e0000;return 0;
 case J_RESET:c.resets++;if(c.mode==14&&c.resets==2)return -1;c.regs[1][0]=c.regs[1][0x800/4]=0;return 0;
 case J_MUX:if(c.mode==17&&c.resets==2)return 0;c.regs[0][0x110c/4]=(c.regs[0][0x110c/4]&~0xf00000)|((a&15)<<20);return 0;
 case J_VTOP:return 0xa1c00000u+(uint32_t)a-0x90000000u+(c.mode==3?16:0);
 case J_COUNT:return c.regs[1][0x280/4]>>3;
 case J_ENCODE:{
  struct jpeg_task *t=(void*)a;assert(t->packet[0]==64&&t->packet[1]==32&&t->packet[2]==2&&t->packet[3]==2&&t->packet[4]==64);
  assert(t->planes[0]==0x90000000+c.source&&t->planes[1]==0x90000800+c.source&&t->planes[2]==0x90008000);
  for(unsigned i=0;i<128;i++)assert(t->quant[i]==16);
  c.jobs++;c.selector=5;c.error=0;
  if(c.mode==7)return -1;
  memcpy(c.memory+JPEG_OUT,c.preserve?fixture_effect:fixture_reference,c.preserve?sizeof fixture_effect:sizeof fixture_reference);c.regs[1][0x280/4]=sizeof fixture*8;
  c.regs[1][0x810/4]=0xa1c00000+c.source;c.regs[1][0x814/4]=0xa1c00800+c.source;c.regs[1][0x818/4]=0xa1c08000;
  c.regs[1][8/4]=c.regs[1][0x808/4]=(64<<16)|32;c.regs[1][0x80c/4]=64;
  if(c.mode==8)c.memory[c.source+4]^=1;
  if(c.mode==9)c.memory[BITB_BYTES-1]^=1;
  if(c.mode==10)c.memory[JPEG_OUT]=0;
  if(c.mode==11)c.regs[1][0x280/4]=JPEG_CAP*8;
  if(c.mode==16)c.regs[1][0x818/4]+=16;
  return 0;
 }
 }
 assert(0);return -1;
}
int main(void){
 assert(jpeg_parse(fixture,sizeof fixture));
 for(unsigned n=0;n<sizeof fixture;n++)assert(!jpeg_parse(fixture,n));
 unsigned char broken[sizeof fixture];memcpy(broken,fixture,sizeof fixture);broken[sizeof fixture-1]=0;assert(!jpeg_parse(broken,sizeof broken));
 for(unsigned variant=0;variant<2;variant++)for(unsigned mode=0;mode<=18;mode++){
  free(full_pad);full_pad=NULL;full_pad_bytes=full_initialized=full_stage=full_prepare_unsafe=0;memset(&c,0,sizeof c);memset(&s,0,sizeof s);c.mode=mode;c.source=variant?0x1000:0;c.preserve=variant;c.selector=3;
  if(variant){full_cache(123);full_initialized=1;full_stage=FULL_LUT;for(unsigned off=0;off<BITB_BYTES;off+=TEST_BLOCK)full_render(c.memory+off,off,1,NULL,0);struct bitb_request prev={BITB_REQUEST_MAGIC,10,0xa1c00000,BITB_BYTES,123,99,0x91000000,0,0};prev.crc=bitb_crc(&prev,32);assert(full_capture(&full_lut_after,&prev,c.memory,1,NULL,0));}c.regs[0][0x110c/4]=0x300000;
  if(mode==2)c.regs[0][0x1020/4]=0x80;
  struct bitb_request q={BITB_REQUEST_MAGIC,10,0xa1c00000,BITB_BYTES,123,99,0x91000000,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
  struct bitb_result r={.ready=1,.paused_contract=1,.alias_va=0x90000000};struct jpeg_meta j={0};
  struct jpeg_ops ops={&c,rd,wr,call,ctl,state,idle,barrier,note,kw};
  int rc=jpeg_run_input(&q,&r,c.memory,&s,&j,&ops,c.source,c.preserve);
  if(mode==0){assert(r.source_pa==q.pa+c.source&&r.source_copies==!variant);assert(!rc&&r.fresh_completion&&r.safe_to_release&&r.restore_verified&&r.source_ok&&r.target_ok&&r.guards_ok&&r.kernel_restored);}
  else{assert(rc);if(mode==5||mode==6||mode==7||mode==16)assert(!r.safe_to_release&&r.quarantine_reason==1&&c.resets==1);}
  if(mode==8||mode==9||mode==10||mode==11)assert(r.safe_to_release&&c.resets==2);
  if(mode==12||mode==13||mode==14||mode==15||mode==17||mode==18)assert(!r.safe_to_release);
  assert(c.jobs<=1&&!c.locked);
 }
 puts("PASS 38 actual JPEG-engine models and all truncated fixture lengths; no hardware");return 0;
}
