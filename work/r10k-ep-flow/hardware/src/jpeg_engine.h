/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_JPEG_ENGINE_H
#define NXL_JPEG_ENGINE_H
#include "jpeg_core.h"
#include "shared_memory.h"
/* Pinned original d5_ep_jpeg_encoder packet, replayed with original ARM code.
 * Quantization is our own uniform Q=16, not a copied manufacturer table.
 */
struct jpeg_task {uint32_t packet[10],planes[3],tables[3];unsigned char quant[128];};
struct jpeg_snapshot {uint32_t top_mux,jpeg[10];struct jpeg_task task;};
static const uint32_t jpeg_offsets[10]={4,8,0x284,0x804,0x808,0x80c,0x810,0x814,0x818,0x820};
enum jpeg_call {J_CLOCK,J_RESET,J_MUX,J_ENCODE,J_COUNT,J_VTOP};
struct jpeg_ops {
 void *ctx;uint32_t (*rd)(void*,unsigned,uint32_t);void (*wr)(void*,unsigned,uint32_t,uint32_t);
 int (*call)(void*,enum jpeg_call,uintptr_t,uint32_t);int (*ctl)(void*,uint32_t,void*);
 int (*state)(void*,unsigned);int (*idle)(void*);void (*barrier)(void*);void (*note)(void*,unsigned);
 uint32_t (*kernel_word)(void*,uint32_t);
};
static int jpeg_run_input(const struct bitb_request *q,struct bitb_result *r,volatile unsigned char *m,struct jpeg_snapshot *s,struct jpeg_meta *j,const struct jpeg_ops *o,unsigned source,int preserve){
 int failure=0,locked=0,touched=0,outstanding=0,enabled2=0,enabled5=0,have=0;
 uint32_t selector2=2,selector5=5,wait5[3]={5000,0,5},select_old=0;
#define NEED(c,e) do{if(!(c)){failure=(e);goto cleanup;}}while(0)
#define RD(b,a) o->rd(o->ctx,b,a)
#define WR(b,a,v) o->wr(o->ctx,b,a,v)
#define NOTE(n) o->note(o->ctx,n)
 r->p11_stage=1;r->pixel_format=422;r->safe_to_release=1;
 NEED(bitb_request_valid(q)&&r->ready&&r->paused_contract&&source==TEST_OUT&&preserve,BJ_REQUEST);
 NEED(o->state(o->ctx,0)&&o->idle(o->ctx),BJ_BUSY);r->idle_verified=1;
 NEED(o->ctl(o->ctx,0x6828,0)==0,BJ_LOCK);locked=1;r->lock_acquired=1;
 NEED(o->state(o->ctx,1)&&o->idle(o->ctx),BJ_LOCK_STATE);NOTE(101);
 r->clock_before[0]=RD(0,0x1020);r->clock_before[1]=RD(0,0x1024);
 /* Never probe a clock-gated JPEG block. */
 NEED(!(r->clock_before[0]&0x80)&&!(r->clock_before[1]&0x1e0000),BJ_CLOCK);
 j->old_selector=o->kernel_word(o->ctx,0xc07555c4);j->error_before=o->kernel_word(o->ctx,0xc07554c4);
 NEED((j->old_selector==3||j->old_selector==5)&&!j->error_before,BJ_CONTEXT);
 select_old=j->old_selector==3?1:0;s->top_mux=RD(0,0x110c);
 for(unsigned i=0;i<10;i++)s->jpeg[i]=RD(1,jpeg_offsets[i]);
 have=1;r->inactive_start_verified=1;r->backup_skipped=1;
 r6_timing("VTOP_ALL_PAGES","BEGIN",TEST_PAGES,0);
 for(unsigned i=0;i<TEST_TRANSLATIONS;i++){uint32_t off=test_translation_offset(i);int pa=o->call(o->ctx,J_VTOP,r->alias_va+off,0);r->translations++;r->matches+=(uint32_t)pa==q->pa+off;}
 r6_timing("VTOP_ALL_PAGES","END",r->translations,r->matches==TEST_TRANSLATIONS);
 NEED(r->matches==TEST_TRANSLATIONS,BJ_TRANSLATION);
 NEED(full_begin(preserve?FULL_FINAL:FULL_REFERENCE,q,r,m),BJ_PREPARE);
 touched=1;r->mmio_modified=1;r->p11_stage=2;
 o->call(o->ctx,J_CLOCK,13,0);r->cold_restart_required=1;r->local_reset_calls++;
 NEED(o->call(o->ctx,J_RESET,13,0)==0,BJ_RESET);o->barrier(o->ctx);NOTE(201);
 NEED(!(RD(1,0)&1)&&!(RD(1,0x800)&1),BJ_RESET);
 o->call(o->ctx,J_MUX,0,0);o->barrier(o->ctx);
 /* ACC1 is an unused DMA notification in tint::encodeYcc: it never waits on
  * it. Do not enable or clear its stale software count. ACC2 forwards errors
  * to the selected queue; ACC5 is the original encoder completion. */
 WR(0,0x1060,0x26);o->barrier(o->ctx);
 NEED(!RD(0,0x10d8)&&o->ctl(o->ctx,0x400c6809,wait5)==0&&o->state(o->ctx,1),BJ_QUEUE);
 r->queue_initialized=r->queue_fresh=1;
 NEED(o->ctl(o->ctx,0x40046810,&selector2)==0,BJ_ENABLE);enabled2=1;
 NEED(o->ctl(o->ctx,0x40046810,&selector5)==0,BJ_ENABLE);enabled5=1;r->irq_enabled=1;
 NEED(o->state(o->ctx,2)&&!RD(0,0x10d8),BJ_QUEUE);
 struct jpeg_task *t=&s->task;memset(t,0,sizeof *t);
 t->packet[0]=JPEG_W;t->packet[1]=JPEG_H;t->packet[2]=1;t->packet[3]=2;t->packet[4]=JPEG_W;
 t->packet[6]=(uint32_t)(uintptr_t)t->planes;t->packet[7]=(uint32_t)(uintptr_t)t->tables;
 t->planes[0]=r->alias_va+source;t->planes[1]=r->alias_va+source+JPEG_Y;t->planes[2]=r->alias_va+JPEG_OUT;
#ifdef NXL_FAST_PIPELINE
 NEED(nj_valid(&full_jpeg_config,full_workspace_original.source_sha256),BJ_PREPARE);
 memcpy(t->quant,full_jpeg_config.quant,sizeof t->quant);
#else
 memset(t->quant,16,sizeof t->quant);
#endif
 t->tables[0]=(uint32_t)(uintptr_t)t->quant;t->tables[1]=(uint32_t)(uintptr_t)(t->quant+64);
 j->quant_crc=bitb_crc(t->quant,sizeof t->quant);NOTE(301);
 r->p11_stage=3;r->submission_attempted=r->engine_jobs=1;outstanding=1;r->safe_to_release=0;
 r->source_pa=q->pa+source;r->source_copies=0;
 r->copy_return=o->call(o->ctx,J_ENCODE,(uintptr_t)t,0);j->native_return=r->copy_return;o->barrier(o->ctx);NOTE(302);
 NEED(!r->copy_return,BJ_COPY);
 /* Translation/config is checked after the native atomic configure+submit. */
 NEED(RD(1,0x810)==q->pa+source&&RD(1,0x814)==q->pa+source+JPEG_Y&&RD(1,0x818)==q->pa+JPEG_OUT,BJ_TRANSLATION);
 NEED((RD(1,0x804)&3u)==1u&&((RD(1,4)>>5)&3u)==1u,BJ_PREPARE);
 NEED(RD(1,8)==((JPEG_W<<16)|JPEG_H)&&RD(1,0x808)==((JPEG_W<<16)|JPEG_H)&&RD(1,0x80c)==JPEG_W,BJ_PREPARE);
 r->p11_stage=4;r->wait_return=o->ctl(o->ctx,0xc00c6808,wait5);o->barrier(o->ctx);NOTE(401);
 j->error_after=o->kernel_word(o->ctx,0xc07554c4);
 NEED(r->wait_return>0&&!j->error_after,BJ_WAIT);
 outstanding=0;r->fresh_completion=1;
 j->stream_bits=RD(1,0x280);j->stream_bytes=(uint32_t)o->call(o->ctx,J_COUNT,0,0);r->pixel_bytes=j->stream_bytes;
 NEED(j->stream_bytes==(j->stream_bits>>3)&&!(j->stream_bits&7)&&j->stream_bytes>=32&&j->stream_bytes<=JPEG_CAP-JPEG_PAD,BJ_COMPARE);
 r->p11_stage=5;o->barrier(o->ctx);
 int memory_ok=full_finish(FULL_FINAL,q,r,m,j->stream_bytes);
 struct jpeg_info actual;
#ifdef NXL_FAST_PIPELINE
 int actual_ok=full_pad&&full_jpeg_info_valid&&full_pad_bytes==j->stream_bytes;actual=full_jpeg_info;
#else
 int actual_ok=full_pad&&jpeg_parse_info(full_pad,j->stream_bytes,&actual);
#endif
 j->structural=actual_ok;j->format=actual_ok?actual.format:0;j->source_ok=r->source_ok;j->guards_ok=r->guards_ok;j->changed_extent=full_pad_bytes;NOTE(501);
 NEED(memory_ok,BJ_COMPARE);

cleanup:
 if(failure)r->failed_stage=r->p11_stage;
 if(locked){
  if(enabled5){if(o->ctl(o->ctx,0x40046811,&selector5))failure=BJ_DISABLE;else enabled5=0;}
  if(enabled2){if(o->ctl(o->ctx,0x40046811,&selector2))failure=BJ_DISABLE;else enabled2=0;}
  if(touched&&have&&!outstanding){
   r->p11_stage=6;r->local_reset_calls++;
   if(o->call(o->ctx,J_RESET,13,0))failure=BJ_RESET;
   else {
    o->barrier(o->ctx);r->reset_after=!(RD(1,0)&1)&&!(RD(1,0x800)&1);
    /* Restore ordinary configuration, never replay old start/reset/IRQ bits.
     * Internal quantizer SRAM is deliberately not claimed as restored. */
    for(unsigned i=0;i<10;i++)WR(1,jpeg_offsets[i],s->jpeg[i]);
    o->call(o->ctx,J_MUX,(s->top_mux>>20)&15,0);o->barrier(o->ctx);
    r->registers_restored=RD(0,0x110c)==s->top_mux;
    for(unsigned i=0;i<10;i++)if(RD(1,jpeg_offsets[i])!=s->jpeg[i])r->registers_restored=0;
    if(o->ctl(o->ctx,0xc0046816,&select_old))r->registers_restored=0;
    WR(0,0x1060,0x26);o->barrier(o->ctx);
    uint32_t masks[2]={0x80,0x1e0000};r->clocks_restored=1;
    for(unsigned i=0;i<2;i++){uint32_t off=0x1020+4*i;WR(0,off,(RD(0,off)&~masks[i])|(r->clock_before[i]&masks[i]));r->clock_after[i]=RD(0,off);if(r->clock_after[i]!=r->clock_before[i])r->clocks_restored=0;}
    r->restore_verified=r->reset_after&&r->registers_restored&&r->clocks_restored&&!RD(0,0x10d8);
    if(!r->restore_verified&&!failure)failure=BJ_RESTORE;
   }
  }else if(!touched)r->restore_verified=1;
  if(o->ctl(o->ctx,0x6829,0)==0)r->lock_released=1;else if(!failure)failure=BJ_UNLOCK;
  r->kernel_restored=o->state(o->ctx,0);r->irq_disabled=!enabled2&&!enabled5&&!RD(0,0x1080);
  r->safe_to_release=!outstanding&&r->lock_released&&r->kernel_restored&&r->irq_disabled&&r->restore_verified&&!full_prepare_unsafe;
  if(!r->safe_to_release&&!failure)failure=BJ_RESTORE;
 }

 r->quarantine_reason=outstanding?1:(!r->safe_to_release?2:0);r->error=failure;r->phase=8;NOTE(999);
 return failure?1:0;
#undef NEED
#undef RD
#undef WR
#undef NOTE
}
#endif
