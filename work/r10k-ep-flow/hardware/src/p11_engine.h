/* SPDX-License-Identifier: AGPL-3.0-only
 * Experimental native 3DLUT task. All DMA buffers belong to one CAP allocation.
 * Firmware configuration contracts are replayed separately from this model.
 */
#ifndef NXL_P11_ENGINE_H
#define NXL_P11_ENGINE_H
#include "test_geometry.h"
#include "shared_memory.h"
#define P11_W TEST_W
#define P11_H TEST_H
#define P11_FRAME TEST_FRAME
#define P11_OUT TEST_OUT
#define P11_ID TEST_TABLE
#define P11_BACKUP TEST_BACKUP
#define P11_READ TEST_READ
#define P11_BACKUP2 TEST_BACKUP2
#define P11_SLOT 19712u
#define P11_NODES 19652u
/* One paired Still format: original source/destination byte40=2 generates
 * LOAD/PROCESS/RDMA/WDMA format1, with p19 verified single-tile geometry. */
#define P20_NATIVE_FORMAT 0u
static const uint32_t p11_offsets[]={0x1000,0x1104,0x110c,0x1400,0x1404,0x1408,0x140c,0x1410,0x1418,0x141c,0x1420,0x1424,0x1428,0x1700,0x1704,0x1708,0x170c,0x1710,0x1718,0x171c,0x1720,0x1724};
#define P11_CONFIG (sizeof p11_offsets/sizeof p11_offsets[0])
enum p11_call {P11_CLOCK,P11_UPDATE,P11_LOAD,P11_SAVE,P11_PROCESS,P11_RDMA,P11_WDMA,P11_MUXR,P11_MUXW,P11_START,P11_VTOP,P11_SELECT,P13_RESET,P11_DMA_RESET};
struct p11_snapshot {uint32_t lut_before[5],lut_after[5],rdma[26],wdma[26],start[9];};
struct p11_ops {
 void *ctx;
 uint32_t (*rd)(void*,unsigned,uint32_t);
 void (*wr)(void*,unsigned,uint32_t,uint32_t);
 int (*call)(void*,enum p11_call,uintptr_t,uint32_t,uint32_t);
 int (*ctl)(void*,uint32_t,void*);
 int (*state)(void*,unsigned);
 int (*idle)(void*);
 uint32_t (*ms)(void*);
 void (*delay)(void*);
 void (*barrier)(void*);
 void (*checkpoint)(void*,uint32_t);
};
/* Match the original high-level access ordering: update TOP (16), LUT (18),
 * then invoke the low-level access API which emits its own pulse. */
static void p12_note(const struct p11_ops *o,unsigned step){if(o->checkpoint)o->checkpoint(o->ctx,step);}
static int p12_access(const struct p11_ops *o,unsigned stage,enum p11_call kind,uintptr_t va,uint32_t bank,uint32_t format){
 p12_note(o,stage*100+1);o->call(o->ctx,P11_UPDATE,16,0,0);o->call(o->ctx,P11_UPDATE,18,0,0);o->barrier(o->ctx);
 int rc=o->call(o->ctx,kind,va,bank,format);o->barrier(o->ctx);p12_note(o,stage*100+2);return rc;
}
static void p12_access_complete(const struct p11_ops *o,unsigned stage){o->call(o->ctx,P11_UPDATE,16,0,0);o->call(o->ctx,P11_UPDATE,18,0,0);o->barrier(o->ctx);p12_note(o,stage*100+4);}
static int p12_wait(const struct p11_ops *o,unsigned stage,uint32_t command,void *arg){int rc=o->ctl(o->ctx,command,arg);p12_note(o,stage*100+3);return rc;}
static int p11_slot(unsigned i){return (i>=P11_BACKUP&&i<P11_BACKUP+P11_SLOT)||(i>=P11_READ&&i<P11_READ+P11_SLOT)||(i>=P11_BACKUP2&&i<P11_BACKUP2+P11_SLOT);}
static int p11_same(volatile const unsigned char *a,volatile const unsigned char *b,unsigned bytes){for(unsigned i=0;i<bytes;i++)if(a[i]!=b[i])return 0;return 1;}
static void p11_dma(uint32_t p[26],uint32_t va,int write){
 memset(p,0,26*4);p[0]=va;p[1]=va+(write?TEST_Y:SW_FRONT_UV);p[2]=write?TEST_W:SW_FRONT_STRIDE;p[3]=8;p[4]=1;p[5]=P20_NATIVE_FORMAT;p[6]=1;p[8]=1;p[10]=128;
 /* NX500 CBackend_3dlut_Still (8011d520/8011d358; constructor 8011e90c).
  * RDMA packed byte 72 maps to Linux flat word 25 / CTRL2 bits 8..11.
  * Legacy 803fb068 uses 0; the actual Still builder uses 1.
  * WDMA backend byte4=1 swaps flags 7/8; both encode the same CTRL2 0x30. */
 if(write){p[7]=1;p[8]=0;}else p[25]=1;
 p[write?16:17]=TEST_W-1;p[write?17:18]=TEST_H-1;
 p[write?20:21]=P11_W;p[write?21:22]=P11_H;
}
_Static_assert(P11_ID+P11_SLOT<=P11_BACKUP,"LUT table region");
_Static_assert(P11_BACKUP+P11_SLOT<=P11_READ,"backup regions");
_Static_assert(P11_READ+P11_SLOT<=P11_BACKUP2 && P11_BACKUP2+P11_SLOT<=BITB_BYTES,"bounded backup slots");
/* P13 is an inactive-module initialization experiment, not a bank-preserving
 * live takeover. Local reset/temporary SRAM use always requires a cold restart. */
static int p11_run(const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m,struct p11_snapshot *s,const struct p11_ops *o){
 int failure=0,locked=0,touched=0,outstanding=0,core=0,acc=0,have=0;uint32_t old_acc=0,selector7=7,wait7[3]={250,0,7},selector5=5,wait5[2]={3000,5};
#define P13_NEED(c,e) do{if(!(c)){failure=(e);goto cleanup;}}while(0)
#define RD(b,a) o->rd(o->ctx,b,a)
#define WR(b,a,v) o->wr(o->ctx,b,a,v)
 r->p11_stage=1;r->pixel_format=422;r->pixel_bytes=P11_FRAME;r->safe_to_release=1;
 P13_NEED(bitb_request_valid(q)&&r->ready&&r->paused_contract,BJ_REQUEST);
 P13_NEED(o->state(o->ctx,0)&&o->idle(o->ctx),BJ_BUSY);r->idle_verified=1;
 P13_NEED(o->ctl(o->ctx,0x6828,0)==0,BJ_LOCK);locked=1;r->lock_acquired=1;
 P13_NEED(o->state(o->ctx,1)&&o->idle(o->ctx),BJ_LOCK_STATE);p12_note(o,101);
 for(unsigned i=0;i<2;i++)r->clock_before[i]=RD(0,0x1020+i*4);
 /* P12 demonstrated this exact ungated playback baseline. Never read a gated
  * module to infer whether it is inactive, and never reset an active owner. */
 P13_NEED(!((r->clock_before[0]|r->clock_before[1])&16),BJ_CLOCK);
 P13_NEED(RD(0,0x1048)==0xaeaaaaaau&&!RD(0,0x1100)&&!RD(0,0x1200),BJ_CONTEXT);
 for(unsigned i=0;i<P11_CONFIG;i++)r->config_before[i]=RD(0,p11_offsets[i]);
 for(unsigned i=0;i<5;i++){s->lut_before[i]=RD(1,i*4);P13_NEED(!s->lut_before[i],BJ_BUSY);}
 r->control_before=RD(0,0x1070);old_acc=RD(0,0x1080);P13_NEED(!r->control_before&&!old_acc,BJ_BUSY);
 have=1;r->inactive_start_verified=1;r->backup_skipped=1;
 for(unsigned i=0;i<TEST_TRANSLATIONS;i++){uint32_t off=test_translation_offset(i);int pa=o->call(o->ctx,P11_VTOP,r->alias_va+off,0,0);r->translations++;r->matches+=(uint32_t)pa==q->pa+off;}
 P13_NEED(r->matches==TEST_TRANSLATIONS,BJ_TRANSLATION);
 P13_NEED(full_begin(FULL_LUT,q,r,m),BJ_PREPARE);
 touched=1;r->mmio_modified=1;r->p11_stage=2;
 o->call(o->ctx,P11_SELECT,0,0,0);o->call(o->ctx,P11_CLOCK,3,0,0);o->barrier(o->ctx);
 r->cold_restart_required=1;r->local_reset_calls++;p12_note(o,201);
 P13_NEED(o->call(o->ctx,P13_RESET,3,0,0)==0,BJ_RESET);o->barrier(o->ctx);p12_note(o,202);
 /* This stage owns RDMA1 and WDMA0. A LUT-core reset alone leaves the
  * shared DMA FSM epoch intact. Never reset a submitted/uncertain job. */
 P13_NEED(o->call(o->ctx,P11_DMA_RESET,1,0,0)==0&&o->call(o->ctx,P11_DMA_RESET,16,0,0)==0,BJ_RESET);o->barrier(o->ctx);
 for(unsigned i=0;i<5;i++)P13_NEED(!RD(1,i*4),BJ_RESET);
 o->call(o->ctx,P11_MUXR,1,1,0);o->call(o->ctx,P11_MUXW,1,0,0);o->call(o->ctx,P11_UPDATE,16,0,0);o->barrier(o->ctx);p12_note(o,102);
 /* No SRAM SAVE is attempted. The pinned RTOS ISR filters by its software
  * mask (verified read-only at runtime), which must be zero. Linux owns only
  * this freshly initialized ACC7 queue for the duration of the table load. */
 r->p11_stage=3;P13_NEED(!RD(0,0x1080),BJ_ENABLE);
 WR(0,0x1060,0x80);o->barrier(o->ctx);P13_NEED(!(RD(0,0x10d8)&0x80),BJ_QUEUE);
 P13_NEED(o->ctl(o->ctx,0x400c6809,wait7)==0&&o->state(o->ctx,1),BJ_QUEUE);
 P13_NEED(o->ctl(o->ctx,0x40046810,&selector7)==0,BJ_ENABLE);acc=1;
 P13_NEED(o->state(o->ctx,2)&&!(RD(0,0x10d8)&0x80),BJ_QUEUE);
 outstanding=1;r->safe_to_release=0;r->table_operations++;
 P13_NEED(p12_access(o,3,P11_LOAD,r->alias_va+P11_ID,0,P20_NATIVE_FORMAT)==0,BJ_LUT);
 r->wait_return=p12_wait(o,3,0xc00c6808,wait7);P13_NEED(r->wait_return>0,BJ_WAIT);
 outstanding=0;r->table_completions=1;r->table_loaded=1;
 P13_NEED(o->ctl(o->ctx,0x40046811,&selector7)==0,BJ_DISABLE);acc=0;o->barrier(o->ctx);p12_access_complete(o,3);
 P13_NEED(!RD(0,0x10d8)&&!RD(0,0x10d0)&&!RD(0,0x10d4),BJ_LUT);
 P13_NEED((RD(1,4)&0x1133u)==(P20_NATIVE_FORMAT<<8),BJ_LUT);
 r->p11_stage=5;p12_note(o,501);
 {uint32_t process[4]={P20_NATIVE_FORMAT,0,0,0};P13_NEED(o->call(o->ctx,P11_PROCESS,(uintptr_t)process,0,0)==0,BJ_LUT);}
 r->source_pa=q->pa+TEST_IN;r->source_copies=0;
 p11_dma(s->rdma,r->alias_va+TEST_IN,0);p11_dma(s->wdma,r->alias_va+P11_OUT,1);
 P13_NEED(o->call(o->ctx,P11_RDMA,(uintptr_t)s->rdma,1,0)==0&&o->call(o->ctx,P11_WDMA,(uintptr_t)s->wdma,0,0)==0,BJ_PREPARE);
 o->call(o->ctx,P11_UPDATE,1,0,0);o->call(o->ctx,P11_UPDATE,4,0,0);o->call(o->ctx,P11_UPDATE,16,0,0);o->call(o->ctx,P11_UPDATE,18,0,0);o->barrier(o->ctx);
 P13_NEED((RD(1,0)&1)&&!(RD(1,8)&1)&&(RD(1,4)&0x1133)==(P20_NATIVE_FORMAT<<8),BJ_PREPARE);
 P13_NEED((RD(0,0x1410)&0xf00u)==0x100u,BJ_PREPARE);
 P13_NEED(((RD(0,0x1408)>>4)&3u)==P20_NATIVE_FORMAT&&((RD(0,0x1708)>>4)&3u)==P20_NATIVE_FORMAT,BJ_PREPARE);
 P13_NEED(RD(0,0x1424)==((P11_W<<16)|P11_H)&&RD(0,0x1724)==((P11_W<<16)|P11_H),BJ_PREPARE);
 P13_NEED(RD(0,0x1400)==q->pa+TEST_IN&&RD(0,0x1404)==q->pa+TEST_IN+SW_FRONT_UV&&RD(0,0x1700)==q->pa+P11_OUT&&RD(0,0x1704)==q->pa+P11_OUT+TEST_Y,BJ_TRANSLATION);
 WR(0,0x1050,0x20);o->barrier(o->ctx);P13_NEED(!(RD(0,0x10c8)&0x20),BJ_QUEUE);
 P13_NEED(o->ctl(o->ctx,0x40086807,wait5)==0&&o->ctl(o->ctx,0x4004680e,&selector5)==0,BJ_QUEUE);core=1;
 P13_NEED(o->state(o->ctx,3)&&!(RD(0,0x10c8)&0x20),BJ_QUEUE);
 s->start[0]=2;s->start[1]=1;s->start[2]=s->start[3]=~0u;s->start[4]=0;s->start[5]=s->start[6]=s->start[7]=~0u;s->start[8]=0;
 p12_note(o,502);r->p11_stage=6;r->submission_attempted=1;r->engine_jobs=1;r->lut_hardware_jobs=1;outstanding=1;r->safe_to_release=0;
 p12_note(o,601);o->call(o->ctx,P11_START,(uintptr_t)s->start,0,0);o->barrier(o->ctx);p12_note(o,602);
 r->wait_return=p12_wait(o,6,0x40086806,wait5);P13_NEED(r->wait_return>0,BJ_WAIT);outstanding=0;r->fresh_completion=1;
 P13_NEED(o->ctl(o->ctx,0x4004680f,&selector5)==0,BJ_DISABLE);core=0;o->barrier(o->ctx);
 P13_NEED(!RD(0,0x10cc)&&!RD(0,0x10d0)&&!RD(0,0x10d4)&&!RD(0,0x10d8)&&!(RD(0,0x10c8)&~0x20u),BJ_WAIT);
 if(!full_finish(FULL_LUT,q,r,m,0))failure=BJ_COMPARE;

cleanup:
 p12_note(o,r->p11_stage*100+9);if(failure)r->failed_stage=r->p11_stage;
 if(locked){
  if(core){if(o->ctl(o->ctx,0x4004680f,&selector5))failure=BJ_DISABLE;else core=0;}
  if(acc){if(o->ctl(o->ctx,0x40046811,&selector7))failure=BJ_DISABLE;else acc=0;}
  if(touched&&have&&!outstanding){
   r->p11_stage=7;r->local_reset_calls++;p12_note(o,701);
   if(o->call(o->ctx,P13_RESET,3,0,0)!=0){if(!failure)failure=BJ_RESET;}
   else {
    o->barrier(o->ctx);p12_note(o,702);
    for(unsigned i=0;i<P11_CONFIG;i++)WR(0,p11_offsets[i],r->config_before[i]);
    WR(1,4,s->lut_before[1]);WR(1,12,s->lut_before[3]);WR(1,16,s->lut_before[4]);WR(1,8,s->lut_before[2]);WR(1,0,s->lut_before[0]);
    o->call(o->ctx,P11_UPDATE,1,0,0);o->call(o->ctx,P11_UPDATE,4,0,0);o->call(o->ctx,P11_UPDATE,16,0,0);o->call(o->ctx,P11_UPDATE,18,0,0);o->barrier(o->ctx);
    r->registers_restored=1;
    for(unsigned i=0;i<P11_CONFIG;i++){r->config_after[i]=RD(0,p11_offsets[i]);if(r->config_after[i]!=r->config_before[i])r->registers_restored=0;}
    for(unsigned i=0;i<5;i++){s->lut_after[i]=RD(1,i*4);if(s->lut_after[i]!=s->lut_before[i])r->registers_restored=0;}
    r->reset_after=!(RD(1,0)&1);WR(0,0x1060,0x80);WR(0,0x1050,0x20);o->barrier(o->ctx);
    r->control_after=RD(0,0x1070);p12_note(o,801);r->clocks_restored=1;
    for(unsigned i=0;i<2;i++){uint32_t off=0x1020+i*4;WR(0,off,(RD(0,off)&~16u)|(r->clock_before[i]&16u));r->clock_after[i]=RD(0,off);if((r->clock_after[i]^r->clock_before[i])&16)r->clocks_restored=0;}
    r->restore_verified=r->registers_restored&&r->clocks_restored&&r->reset_after&&r->control_after==r->control_before&&RD(0,0x1080)==old_acc;
    if(!r->restore_verified&&!failure)failure=BJ_RESTORE;
   }
  }else if(!touched)r->restore_verified=1;
  if(o->ctl(o->ctx,0x6829,0)==0)r->lock_released=1;else if(!failure)failure=BJ_UNLOCK;
  p12_note(o,802);r->kernel_restored=o->state(o->ctx,0);r->irq_disabled=!core&&!acc&&!(RD(0,0x1070)&0x20)&&!RD(0,0x1080);
  r->safe_to_release=!outstanding&&r->lock_released&&r->kernel_restored&&r->irq_disabled&&r->restore_verified&&!full_prepare_unsafe;
 }

 if(failure&&!r->failed_stage)r->failed_stage=r->p11_stage;
 if(!r->safe_to_release&&!failure)failure=BJ_RESTORE;
 r->quarantine_reason=outstanding?1:(!r->safe_to_release?2:0);r->error=failure;r->phase=8;return failure?1:0;
#undef P13_NEED
#undef RD
#undef WR
}
#endif
