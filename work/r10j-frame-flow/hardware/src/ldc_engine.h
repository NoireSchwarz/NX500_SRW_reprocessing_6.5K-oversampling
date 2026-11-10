/* SPDX-License-Identifier: AGPL-3.0-only. Standalone native LDC before LUT.
 * ABI translated against unchanged P7 and Linux ARM configuration functions.
 * Two disjoint strided NV16 frames, one existing CAP, no pixel CPU transform. */
#ifndef NXL_LDC_ENGINE_H
#define NXL_LDC_ENGINE_H
#include "../../common/lens_profile.h"
#include "p11_engine.h"
#include "ep_standalone_start.h"
#include "../../common/ldc_observation.h"
#define ldc_offsets lh_config_offsets
/* Explicit original memory-to-memory material.
 * This raster source is not the Bayer OTF producer's overlapping tile stream.
 * P7 801fbca4 clamps MtoM tile width to480. Submit this isolated RAM source
 * with the generic native single-memory API, not the continuous View start. */
#define LDC_CONTEXT 0u
#define LDC_TILE_W 480u
#define LDC_TILE_H TEST_H
#define LDC_TABLE_BASE 0x200u
#define LDC_MODE_OFFSET 0x74u
#define LDC_CENTER_OFFSET 0x78u
static const uint32_t ldc_top_offsets[]={0x1000,0x1104,0x110c,0x1300,0x1304,0x1308,0x130c,0x1310,0x1318,0x131c,0x1320,0x1324,0x1328,0x1700,0x1704,0x1708,0x170c,0x1710,0x1718,0x171c,0x1720,0x1724,0x1048,0x1114,0x132c,0x172c};
#define LDC_TOP (sizeof ldc_top_offsets/sizeof *ldc_top_offsets)
#define LDC_REGS (sizeof ldc_offsets/sizeof *ldc_offsets)
enum ldc_call {L_CLOCK,L_RESET,L_SELECT,L_UPDATE,L_CONFIG,L_RDMA,L_WDMA,L_MUXR,L_MUXW,L_START,L_VTOP,L_STOP,L_UPDATE_MODE,L_DMA_RESET};
struct ldc_snapshot {uint32_t top[LDC_TOP],registers[LDC_REGS],table[128],params[27],packed[72],rdma[26],wdma[26];struct ep_standalone_start start;};
_Static_assert(sizeof(struct ldc_snapshot)==1552,"R8x includes auto modes and both channel FSMs");
static const uint32_t ldc_dma_audit_offsets[]={0x1000,0x1020,0x1024,0x1048,0x1070,0x1074,0x1078,0x107c,0x1080,0x10c8,0x10cc,0x10d0,0x10d4,0x10d8,0x1100,0x1008,0x100c,0x1200,0x1104,0x110c,0x1300,0x1304,0x1308,0x130c,0x1310,0x1318,0x131c,0x1320,0x1324,0x1328,0x132c,0x1700,0x1704,0x1708,0x170c,0x1710,0x1718,0x171c,0x1720,0x1724,0x172c,0x1114,0x1214,0x120,0x124};
#define LDC_DMA_AUDIT_WORDS (sizeof ldc_dma_audit_offsets/sizeof *ldc_dma_audit_offsets)
enum {LDC_RDMA_CTRL2_MASK=0xf33u,LDC_RDMA_CTRL2_EXPECTED=0x131u};
/* Separate bounded evidence: keep the saved-state and worker-result ABIs intact.
 * Reads cover the exact table/mode/center guards, before and after committing
 * shadow registers. No pixel DMA may start unless the committed values match. */
struct ldc_config_audit {uint32_t configured,committed,mismatches,first_offset,expected_value,actual_value,mask,top_before[4],top_after[4],expected[66],before[66],after[66],dma_checkpoints,dma_before[LDC_DMA_AUDIT_WORDS],dma_started[LDC_DMA_AUDIT_WORDS],dma_after_wait[LDC_DMA_AUDIT_WORDS];
 uint32_t hw_checkpoints,witness_available,witness_changed_bytes,witness_crc[2];struct lh_snapshot hw[4];unsigned char witness[2][384];
 uint32_t auto_stopped,stop_request_before,stop_ack_before,stop_ack,stop_ack_confirm,stop_request_after,stop_ms,stop_verified,dma_stopped[LDC_DMA_AUDIT_WORDS];
 uint32_t dma_rearm_calls,dma_rearm_channel[2],dma_rearm_status[2];};
struct ldc_ops {void*ctx;uint32_t(*rd)(void*,unsigned,uint32_t);void(*wr)(void*,unsigned,uint32_t,uint32_t);int(*call)(void*,enum ldc_call,uintptr_t,uint32_t,uint32_t);int(*ctl)(void*,uint32_t,void*);int(*state)(void*,unsigned);int(*idle)(void*);void(*barrier)(void*);unsigned ldc_bytes;void(*checkpoint)(void*,unsigned);int(*wait)(void*,uint32_t*);int(*coverage)(void*);uint32_t(*ms)(void*);void(*delay)(void*);};
/* Original View stop requests RDMA0 | WDMA0 (P7 80115e7c, 804b6810).
 * Observe the ACK itself: the original polling function's return value also
 * reports zero on an exhausted loop. Never infer a drain from that return.
 * Called only after fresh CORE5 and stopping auto starts. No abort/reset. */
static int ldc_dma_stop_fence(const struct ldc_ops*o,struct ldc_config_audit*a){
 a->stop_request_before=o->rd(o->ctx,0,0x120);a->stop_ack_before=o->rd(o->ctx,0,0x124);if(a->stop_request_before||a->stop_ack_before||!o->ms||!o->delay)return 0;
 uint32_t begin=o->ms(o->ctx);o->wr(o->ctx,0,0x120,0x401u);o->barrier(o->ctx);
 do{
  a->stop_ack=o->rd(o->ctx,0,0x124);
  if(a->stop_ack==0x401u){o->barrier(o->ctx);a->stop_ack_confirm=o->rd(o->ctx,0,0x124);if(a->stop_ack_confirm==0x401u){a->stop_verified=1;break;}}
  if((uint32_t)(o->ms(o->ctx)-begin)>=50u)break;o->delay(o->ctx);
 }while(1);
 a->stop_ms=o->ms(o->ctx)-begin;o->wr(o->ctx,0,0x120,0);o->barrier(o->ctx);
 a->stop_request_after=o->rd(o->ctx,0,0x120);if(a->stop_request_after)a->stop_verified=0;
 return a->stop_verified;
}
static int ldc_hw_read(void*v,unsigned b,uint32_t off,void*out,unsigned bytes){
 const struct ldc_ops*o=v;if(!lh_known_span(b,off,bytes))return 0;uint32_t*p=out;for(unsigned i=0;i<bytes/4;i++)p[i]=o->rd(o->ctx,b,off+4*i);return 1;
}
static void ldc_hw_capture(const struct ldc_ops*o,struct ldc_config_audit*a,unsigned phase){
 if(!phase||phase>4)return;lh_capture(ldc_hw_read,(void*)o,o->ldc_bytes,phase,a->hw+phase-1);a->hw_checkpoints|=1u<<(phase-1);
}
/* RO progress witness only. Never seed, repair or qualify a pixel plane, and
 * never turn changed bytes into completion / reset / release authority. */
static void ldc_witness(volatile const unsigned char*m,struct ldc_config_audit*a,unsigned after){
 const unsigned rows[]={0,TEST_H/2,TEST_H-1};const unsigned cols[]={0,(TEST_W-64)/2,TEST_W-64};unsigned at=0;unsigned char again[384];
 for(unsigned pass=0;pass<2;pass++)for(unsigned plane=0;plane<2;plane++)for(unsigned k=0;k<3;k++){
  unsigned off=SW_LDC_OUT+(plane?SW_FRONT_UV:0)+rows[k]*SW_FRONT_STRIDE+cols[k];unsigned char*dst=pass?again:a->witness[after];
  unsigned index=plane*192+k*64;for(unsigned j=0;j<64;j++)dst[index+j]=m[off+j];
 }
 a->witness_available|=1u<<after;if(memcmp(again,a->witness[after],sizeof again))a->witness_available|=4u<<after;
 a->witness_crc[after]=bitb_crc(a->witness[after],sizeof a->witness[after]);
 if(after)for(at=0;at<384;at++)a->witness_changed_bytes+=a->witness[0][at]!=a->witness[1][at];
}
static uint32_t ldc_guard_offset(unsigned i){return i<64?LDC_TABLE_BASE+i*4:i==64?LDC_MODE_OFFSET:LDC_CENTER_OFFSET;}
static uint32_t ldc_guard_mask(unsigned i){return i<64?~0u:i==64?0x00ffff1fu:0x1fff1fffu;}
static void ldc_audit_read(const struct ldc_ops*o,uint32_t values[66],uint32_t top[4]){
 for(unsigned i=0;i<66;i++)values[i]=o->rd(o->ctx,1,ldc_guard_offset(i));
 const uint32_t offsets[]={0x1020,0x1024,0x1000,0x1048};
 for(unsigned i=0;i<4;i++)top[i]=o->rd(o->ctx,0,offsets[i]);
}
static void ldc_dma_audit(const struct ldc_ops*o,uint32_t values[LDC_DMA_AUDIT_WORDS]){
 for(unsigned i=0;i<LDC_DMA_AUDIT_WORDS;i++)values[i]=o->rd(o->ctx,0,ldc_dma_audit_offsets[i]);
}
static void ldc_params(uint32_t out[27],uintptr_t packed){
 const uint32_t p[27]={1,6480,4320,6480,4320,6480,4320,0,0,0,LDC_TILE_W,LDC_TILE_H,0,0,228,1,0,128,128,0,0,0,1,0,0,0,0};
 memcpy(out,p,sizeof p);out[23]=(uint32_t)packed;
}
static void ldc_dma(uint32_t out[26],uint32_t address,int write){
 /* Original builders applied to complete raster memory images. Both images
  * have full-height, non-overlapping480-wide blocks, context0. */
 memset(out,0,26*4);out[0]=address;out[1]=address+SW_FRONT_UV;out[2]=SW_FRONT_STRIDE;out[3]=8;
 out[4]=out[6]=out[8]=1;out[10]=128;
 out[write?16:17]=TEST_W-1;out[write?17:18]=TEST_H-1;
 out[write?20:21]=LDC_TILE_W;out[write?21:22]=LDC_TILE_H;
 if(!write){out[11]=1;out[25]=1;}
}
static int ldc_run(const struct bitb_request*q,struct bitb_result*r,volatile unsigned char*m,struct ldc_snapshot*s,struct ldc_config_audit*audit,const struct lp_config*profile,const struct ldc_ops*o){
 int failure=0,locked=0,touched=0,have=0,outstanding=0,core=0;uint32_t core5=5,wait5[2]={5000,5};
 memset(audit,0,sizeof *audit);
#define LN(c,e) do{if(!(c)){failure=(e);goto cleanup;}}while(0)
#define LR(b,a) o->rd(o->ctx,b,a)
#define LW(b,a,v) o->wr(o->ctx,b,a,v)
 r->p11_stage=1;r->pixel_format=422;r->pixel_bytes=SW_FRAME;r->safe_to_release=1;
 LN(bitb_request_valid(q)&&r->ready&&r->paused_contract&&lp_valid(profile,full_workspace_original.source_sha256),BJ_REQUEST);
 LN(o->state(o->ctx,0)&&o->idle(o->ctx),BJ_BUSY);r->idle_verified=1;
 LN(o->ctl(o->ctx,0x6828,0)==0,BJ_LOCK);locked=1;r->lock_acquired=1;
 LN(o->state(o->ctx,1)&&o->idle(o->ctx),BJ_LOCK_STATE);
 r->clock_before[0]=LR(0,0x1020);r->clock_before[1]=LR(0,0x1024);
 LN(!LR(0,0x1100)&&!LR(0,0x1200)&&!LR(0,0x1070)&&!LR(0,0x1080)&&!LR(0,0x1114)&&!LR(0,0x1214)&&!LR(0,0x132c)&&!LR(0,0x172c)&&!LR(0,0x120),BJ_CONTEXT);
 for(unsigned i=0;i<LDC_TOP;i++)s->top[i]=LR(0,ldc_top_offsets[i]);
 for(unsigned i=0;i<TEST_TRANSLATIONS;i++){uint32_t off=test_translation_offset(i);r->translations++;r->matches+=(uint32_t)o->call(o->ctx,L_VTOP,r->alias_va+off,0,0)==q->pa+off;}
 LN(r->matches==TEST_TRANSLATIONS,BJ_TRANSLATION);
 LN(full_begin(FULL_LDC,q,r,m),BJ_PREPARE);
 touched=1;r->mmio_modified=1;o->call(o->ctx,L_CLOCK,2,0,0);o->barrier(o->ctx);
 /* LDC registers are never read while its clock is gated. Save every field
  * modified by the original wrapper plus BOTH complete table banks. */
 for(unsigned i=0;i<LDC_REGS;i++)s->registers[i]=LR(1,ldc_offsets[i]);
 for(unsigned i=0;i<128;i++)s->table[i]=LR(1,0x200+i*4);have=1;
 /* The single-memory submit uses manual update mode2 for RDMA0, WDMA0
  * and LDC17. Do not inherit the continuous View auto mode from the prior
  * candidate; preserve every unrelated two-bit field and restore the old
  * complete SREG1048 value from the snapshot after verified completion. */
 LN(o->call(o->ctx,L_UPDATE_MODE,0,2,0)==0&&o->call(o->ctx,L_UPDATE_MODE,4,2,0)==0&&o->call(o->ctx,L_UPDATE_MODE,17,2,0)==0,BJ_PREPARE);
 o->barrier(o->ctx);LN((LR(0,0x1048)&0x300303u)==0x200202u,BJ_COMPARE);
 o->call(o->ctx,L_SELECT,LDC_CONTEXT,0,0);r->p11_stage=2;r->local_reset_calls++;
 LN(o->call(o->ctx,L_RESET,2,0,0)==0,BJ_RESET);o->barrier(o->ctx);
 /* Native Backend_EpTop::Initialize also resets its DMA FSMs. The LDC
  * core reset above does not reset RDMA0/WDMA0. Re-arm only our channel
  * pair while locked, verified idle and before any queue/pixel submit.
  * DMA reset IDs are0 and16; the later bus-stop ACK uses a different mask. */
 for(unsigned i=0;i<2;i++){
  audit->dma_rearm_channel[i]=i?16u:0u;audit->dma_rearm_calls++;
  audit->dma_rearm_status[i]=(uint32_t)o->call(o->ctx,L_DMA_RESET,audit->dma_rearm_channel[i],0,0);
  LN(!audit->dma_rearm_status[i],BJ_RESET);
 }o->barrier(o->ctx);
 lp_pack(profile,s->packed);ldc_params(s->params,(uintptr_t)s->packed);
 for(unsigned i=0;i<64;i++)audit->expected[i]=s->packed[8+i];
 audit->expected[64]=(s->packed[1]&31u)|((s->packed[0]&65535u)<<8);
 audit->expected[65]=(s->packed[2]&8191u)|((s->packed[3]&8191u)<<16);
 LN(o->call(o->ctx,L_CONFIG,(uintptr_t)s->params,LDC_CONTEXT,0)==0,BJ_PREPARE);
 /* Reuse the original raster material's CS0/table0 geometry with real
  * RDMA0 and WDMA0. Preserve Still/OTF state in bank1. */
 o->barrier(o->ctx);ldc_audit_read(o,audit->before,audit->top_before);audit->configured=1;
 /* Commit TOP/LDC with the original EP API. A setter return only
  * confirms software preparation; it is not a committed-register proof. */
 o->call(o->ctx,L_UPDATE,16,0,0);o->call(o->ctx,L_UPDATE,17,0,0);o->barrier(o->ctx);
 ldc_audit_read(o,audit->after,audit->top_after);audit->committed=1;
 for(unsigned i=0;i<66;i++)if((audit->after[i]&ldc_guard_mask(i))!=audit->expected[i]){
  if(!audit->mismatches){audit->first_offset=ldc_guard_offset(i);audit->expected_value=audit->expected[i];audit->actual_value=audit->after[i];audit->mask=ldc_guard_mask(i);}
  audit->mismatches++;
 }
 ldc_hw_capture(o,audit,1);
 LN(!audit->mismatches,BJ_COMPARE);
 LN(o->call(o->ctx,L_MUXR,0,0,LDC_CONTEXT)==0&&o->call(o->ctx,L_MUXW,0,0,LDC_CONTEXT)==0,BJ_PREPARE);
 ldc_dma(s->rdma,r->alias_va+SW_INPUT,0);ldc_dma(s->wdma,r->alias_va+SW_LDC_OUT,1);
 LN(o->call(o->ctx,L_RDMA,(uintptr_t)s->rdma,0,LDC_CONTEXT)==0&&o->call(o->ctx,L_WDMA,(uintptr_t)s->wdma,0,LDC_CONTEXT)==0,BJ_PREPARE);
 o->call(o->ctx,L_UPDATE,0,0,0);o->call(o->ctx,L_UPDATE,4,0,0);o->call(o->ctx,L_UPDATE,16,0,0);o->call(o->ctx,L_UPDATE,17,0,0);o->barrier(o->ctx);
 LN(LR(0,0x1300)==q->pa+SW_INPUT&&LR(0,0x1304)==q->pa+SW_INPUT+SW_FRONT_UV&&LR(0,0x1700)==q->pa+SW_LDC_OUT&&LR(0,0x1704)==q->pa+SW_LDC_OUT+SW_FRONT_UV,BJ_TRANSLATION);
 LW(0,0x1050,0x20);o->barrier(o->ctx);LN(!(LR(0,0x10c8)&0x20),BJ_QUEUE);
 LN(o->ctl(o->ctx,0x40086807,wait5)==0&&o->ctl(o->ctx,0x4004680e,&core5)==0,BJ_QUEUE);core=1;
 LN(o->state(o->ctx,3)&&!(LR(0,0x10c8)&0x20),BJ_QUEUE);
 ep_single_ldc_m2m_start(&s->start);
 ldc_dma_audit(o,audit->dma_before);audit->dma_checkpoints=1;
 /* Compare the original native LDC control fields before any pixel submit.
  * Reserved bits remain outside the mask. No literal MMIO patch is used. */
 LN((LR(0,0x1000)&0x30u)==0u&&(LR(0,0x1310)&LDC_RDMA_CTRL2_MASK)==LDC_RDMA_CTRL2_EXPECTED&&(LR(0,0x1710)&0x30u)==0x30u,BJ_COMPARE);
 LN(LR(0,0x1324)==((LDC_TILE_W<<16)|LDC_TILE_H)&&LR(0,0x1724)==((LDC_TILE_W<<16)|LDC_TILE_H)&&LR(0,0x1320)==(((TEST_W-1)<<16)|(TEST_H-1))&&LR(0,0x1720)==(((TEST_W-1)<<16)|(TEST_H-1))&&!(LR(0,0x1328)&0xffffu)&&(LR(0,0x1308)&0x7ffu)==0x41u&&(LR(0,0x1708)&0x7ffu)==0x41u,BJ_COMPARE);
 LN((LR(1,0x14)&0x7ffu)==LDC_TILE_W&&(LR(1,0x18)&0x3fffu)==LDC_TILE_H&&!(LR(1,0x14)&0x7ff0000u)&&!(LR(1,0x18)&0xff0000u),BJ_COMPARE);
 ldc_hw_capture(o,audit,2);if(o->checkpoint)o->checkpoint(o->ctx,0);ldc_witness(m,audit,0);
 r->source_pa=q->pa+SW_INPUT;r->route_verified=1;r->route_samples=1;r->p11_stage=3;r->submission_attempted=1;r->engine_jobs=1;r->ldc_hardware_jobs=1;outstanding=1;r->safe_to_release=0;
 if(o->checkpoint)o->checkpoint(o->ctx,1);int start_return=o->call(o->ctx,L_START,(uintptr_t)&s->start,0,0);o->barrier(o->ctx);ldc_dma_audit(o,audit->dma_started);audit->dma_checkpoints|=2;ldc_hw_capture(o,audit,3);if(o->checkpoint)o->checkpoint(o->ctx,2);
 /* The original standalone function sends the two explicit TOP1008 memory
  * submit pulses. Command readback is diagnostic only; require its return
  * and the owned manual update fields, then the fresh CORE5 below.
  * Zero command readback never authorizes completion, reset, or release. */
 LN(start_return==0&&(LR(0,0x1048)&0x300303u)==0x200202u,BJ_COMPARE);
 r->wait_return=o->wait?o->wait(o->ctx,wait5):o->ctl(o->ctx,0x40086806,wait5);if(o->checkpoint)o->checkpoint(o->ctx,3);ldc_dma_audit(o,audit->dma_after_wait);audit->dma_checkpoints|=4;ldc_hw_capture(o,audit,4);ldc_witness(m,audit,1);
 LN(r->wait_return>0,BJ_WAIT);r->fresh_completion=1;
 LN(!LR(0,0x10cc)&&!LR(0,0x10d0)&&!LR(0,0x10d4)&&!LR(0,0x10d8),BJ_WAIT);
 /* CORE5 is frame EOF, not a per-tile software credit. Stop the original
  * automatic path, then require an actual DMA bus-stop ACK before reset
  * or reuse. A timeout must retain all ownership and the live context. */
 LN(o->call(o->ctx,L_STOP,0,0,0)==0,BJ_DMA_DRAIN);o->barrier(o->ctx);
 audit->auto_stopped=!LR(0,0x1114)&&!(LR(0,0x132c)&1u)&&!(LR(0,0x172c)&1u);
 LN(audit->auto_stopped&&ldc_dma_stop_fence(o,audit),BJ_DMA_DRAIN);
 ldc_dma_audit(o,audit->dma_stopped);audit->dma_checkpoints|=8;outstanding=0;if(o->checkpoint)o->checkpoint(o->ctx,5);
 LN(!o->coverage||o->coverage(o->ctx),BJ_TILE_COVERAGE);
 if(!full_finish(FULL_LDC,q,r,m,0))failure=BJ_COMPARE;

cleanup:
 if(failure)r->failed_stage=r->p11_stage;
 if(locked){
  if(core){if(o->ctl(o->ctx,0x4004680f,&core5))failure=BJ_DISABLE;else core=0;}
  if(touched&&have&&!outstanding){
   r->local_reset_calls++;if(o->call(o->ctx,L_RESET,2,0,0))failure=BJ_RESET;else{
    for(unsigned i=0;i<128;i++)LW(1,0x200+i*4,s->table[i]);
    for(unsigned i=0;i<LDC_REGS;i++)LW(1,ldc_offsets[i],s->registers[i]);
    for(unsigned i=0;i<LDC_TOP;i++)LW(0,ldc_top_offsets[i],s->top[i]);
    o->call(o->ctx,L_UPDATE,0,0,0);o->call(o->ctx,L_UPDATE,4,0,0);o->call(o->ctx,L_UPDATE,16,0,0);o->call(o->ctx,L_UPDATE,17,0,0);o->barrier(o->ctx);
    r->registers_restored=1;for(unsigned i=0;i<128;i++)if(LR(1,0x200+i*4)!=s->table[i])r->registers_restored=0;
    for(unsigned i=0;i<LDC_REGS;i++)if(LR(1,ldc_offsets[i])!=s->registers[i])r->registers_restored=0;
    for(unsigned i=0;i<LDC_TOP;i++)if(LR(0,ldc_top_offsets[i])!=s->top[i])r->registers_restored=0;
    LW(0,0x1050,0x20);o->barrier(o->ctx);r->reset_after=!(LR(0,0x1100)|LR(0,0x1200));
    const uint32_t mask=8u;r->clocks_restored=1;
    for(unsigned i=0;i<2;i++){unsigned at=0x1020+i*4;LW(0,at,(LR(0,at)&~mask)|(r->clock_before[i]&mask));r->clock_after[i]=LR(0,at);if(r->clock_after[i]!=r->clock_before[i])r->clocks_restored=0;}
    r->restore_verified=r->registers_restored&&r->reset_after&&r->clocks_restored;if(!r->restore_verified&&!failure)failure=BJ_RESTORE;
   }
  }else if(!touched)r->restore_verified=1;
  if(o->ctl(o->ctx,0x6829,0)==0)r->lock_released=1;else failure=BJ_UNLOCK;
  r->kernel_restored=o->state(o->ctx,0);r->irq_disabled=!core&&!(LR(0,0x1070)&0x20)&&!LR(0,0x1080);
  r->safe_to_release=!outstanding&&r->lock_released&&r->kernel_restored&&r->irq_disabled&&r->restore_verified&&!full_prepare_unsafe&&
   !LR(0,0x10c8)&&!LR(0,0x10cc)&&!LR(0,0x10d0)&&!LR(0,0x10d4)&&!LR(0,0x10d8);
 }
 if(!r->safe_to_release&&!failure)failure=BJ_RESTORE;r->quarantine_reason=outstanding?1:!r->safe_to_release?2:0;r->error=failure;r->phase=8;return failure?1:0;
#undef LN
#undef LR
#undef LW
}
#endif
