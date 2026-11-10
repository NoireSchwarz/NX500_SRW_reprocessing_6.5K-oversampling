/* SPDX-License-Identifier: AGPL-3.0-only. Our relocatable ARM code only.
 * Original Still config/start/IRQ handlers execute unchanged. One-shot listener
 * lives in retained owned RAM. CS1 only; no EP, JPEG or firmware writes.
 */
#include "front_layout.h"
#include "front_diagnostics.h"
#include "still_lifecycle_audit.h"
#define P48_STOP_FN __attribute__((noinline,section(".text.stop")))
#include "stop_contract.h"
#include "batch_contract.h"
static volatile struct p39_context ctx __attribute__((section(".context"),used));
typedef int32_t (*call3)(uint32_t,uint32_t,uint32_t);
static inline volatile uint32_t *ptr(uint32_t a){return (volatile uint32_t*)a;}
static inline void barrier(void){__asm__ volatile("dsb sy":::"memory");}
static int32_t native(uint32_t fn,uint32_t a,uint32_t b,uint32_t c){return ((call3)fn)(a,b,c);}
static int32_t cache(uint32_t a,uint32_t n){return native(ctx.cache,ctx.manager,a,n);}
#include "rtos_still_audit.inc"
#include "fnv_uncached.h"
__attribute__((noinline,unused)) static uint32_t hash(uint32_t a,uint32_t n){return nxl_fnv1a_volatile((const volatile unsigned char*)(uintptr_t)a,n);}
static int idle(void){
 volatile uint32_t *t=ptr(P39_TASK);
 if(t[0]!=0x80592640u||(*(volatile unsigned char*)(P39_TASK+3364))!=7)return 0;
 if(t[3204/4]||t[3208/4]||t[3284/4]||t[3288/4])return 0;
 uint32_t normal=t[3428/4];if(normal<0x81000000u||normal>0x93fff000u||normal&3u||*ptr(normal)!=0x805926c8u)return 0;
 uint32_t owner=*ptr(0x81543d40u);
 if(owner<0x81000000u||owner>0x93fff000u||owner&3u)return 0;
 unsigned owner_state=*(volatile unsigned char*)owner;
#ifdef NXL_COLD_INIT_TEST
 if(owner_state>1u)return 0;
#else
 if(owner_state!=1u)return 0;
#endif
 /* Preview EP/CS0 may be active; its Still child must not be selected. */
 if(*ptr(0x8142d154u)!=0x80582820u||*ptr(0x8142d154u+3444)==0x8142df3cu)return 0;
 return 1;
}
#ifdef NXL_COLD_INIT_TEST
static int configuration_ready(uint32_t owner){
 uint32_t normal=*ptr(P39_TASK+3428);
 return normal>=0x81000000u&&normal<=0x93fff000u&&!(normal&3u)&&*ptr(normal)==0x805926c8u&&
  owner>=0x81000000u&&owner<=0x93fff000u&&!(owner&3u)&&
  (*(volatile unsigned char*)owner)==1u&&
  (*(volatile unsigned char*)(P39_TASK+3364))==2u&&
  *ptr(P39_TASK+3436)==normal&&
  !*ptr(P39_TASK+3204)&&!*ptr(P39_TASK+3208)&&!*ptr(P39_TASK+3284)&&!*ptr(P39_TASK+3288)&&
  *ptr(0x8142d154u)==0x80582820u&&*ptr(0x8142d154u+3444)!=0x8142df3cu;
}
#endif
#include "rtos_stop.inc"
#include "rtos_wb.inc"
#include "rtos_cc.inc"
#include "rtos_gamma.inc"
#include "rtos_isp_compare.inc"
#define P48_FN __attribute__((noinline,section(".text.cc")))
static unsigned batch_seq(void){uint32_t d=ctx.listener-(ctx.cap+P48_BATCH_LISTENER_OFFSET);return d%P48_BATCH_LISTENER_STRIDE||d/P48_BATCH_LISTENER_STRIDE>=P49_FRAME_COUNT?0:d/P48_BATCH_LISTENER_STRIDE+1;}
static unsigned batch_source(unsigned seq){return seq==1u?0u:UINT32_MAX;}
static unsigned batch_flags(unsigned seq){return seq==1u?3u:UINT32_MAX;}
static volatile uint32_t *batch_reply(void){return ptr(ctx.cap+P48_BATCH_CONTROL_OFFSET+0x100u);}
static void batch_copy(uint32_t *out,const volatile uint32_t *in,unsigned n){for(unsigned i=0;i<n;i++)out[i]=in[i];barrier();}
static int batch_equal(const uint32_t *a,const uint32_t *b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
static P48_FN int batch_request(uint32_t out[32],unsigned seq,int staged){
 uint32_t other[32];volatile uint32_t *q=ptr(ctx.cap+P48_BATCH_CONTROL_OFFSET);
 batch_copy(out,q,32);batch_copy(other,q,32);if(!batch_equal(out,other,32))return 0;
 return staged?p48_batch_request_staged_valid(out,ctx.nonce,seq,P49_FRAME_COUNT,batch_source(seq),batch_flags(seq)):p48_batch_request_valid(out,ctx.nonce,seq,P49_FRAME_COUNT,batch_source(seq),batch_flags(seq));
}
static P48_FN int batch_bind_requests(const uint32_t *q,unsigned seq){
 uint32_t w[64],c[64],again[64];batch_copy(w,wb(),64);batch_copy(again,wb(),64);if(!batch_equal(w,again,64))return 0;
 batch_copy(c,cc(),64);batch_copy(again,cc(),64);if(!batch_equal(c,again,64))return 0;
 return p41_request_valid(w,ctx.nonce)&&p48_cc_request_valid(c,ctx.nonce)&&w[48]==seq-1&&w[49]==(q[7]&1u)&&w[50]==((q[7]>>1)&1u)&&c[4]==q[6]&&c[27]==seq&&c[3]==w[50]&&c[5]==w[4]&&q[10]==p48_batch_crc(w,256)&&q[11]==p48_batch_crc(c,256);
}
static __attribute__((noinline)) int batch_previous(unsigned seq,uint32_t *saved_crc,uint32_t *idle_crc){
 uint32_t old[64],again[64],saved[32],other[32];volatile uint32_t *base=ptr(ctx.cap+P48_BATCH_CONTROL_OFFSET);
 *saved_crc=*idle_crc=0;if(seq==1){for(unsigned i=32;i<128;i++)if(base[i])return 0;return 1;}
 batch_copy(old,base+64,64);batch_copy(again,base+64,64);batch_copy(saved,base+32,32);batch_copy(other,base+32,32);
 if(!batch_equal(old,again,64)||!batch_equal(saved,other,32)||!p48_batch_reply_valid(old,ctx.nonce,seq-1,P49_FRAME_COUNT)||old[4]!=P48_PHASE_DONE||old[31]||old[32])return 0;
 if(old[10]!=batch_source(seq-1)||old[11]!=batch_flags(seq-1)||old[23]!=p48_batch_listener(ctx.cap,seq-2))return 0;
 /* Current Q has already changed. Bind SAVED directly to the retained DONE,
  * never to current Q or to a fabricated previous physical request. */
 if(saved[0]!=P48_BATCH_SAVED_MAGIC||saved[1]!=1||saved[2]!=ctx.nonce||saved[3]!=seq-1||saved[4]!=seq-2||saved[5]!=old[12]||saved[6]!=old[13]||saved[7]!=1||saved[8]!=old[29]||saved[9]!=old[62]||saved[31]!=seq-1||!p48_batch_zero(saved,10,30)||saved[30]!=p48_batch_crc(saved,120))return 0;
 uint32_t h[64],h2[64];batch_copy(h,ptr(ctx.cap+P48_IDLE_HANDOFF_OFFSET),64);batch_copy(h2,ptr(ctx.cap+P48_IDLE_HANDOFF_OFFSET),64);
 if(!batch_equal(h,h2,64)||!p48_idle_handoff_valid(h,ctx.nonce,seq-1)||h[62]!=old[P48_R_IDLE_HANDOFF_CRC])return 0;
 *saved_crc=saved[30];*idle_crc=h[62];return 1;
}
static P48_FN void batch_init(unsigned seq,uint32_t saved_crc,uint32_t idle_crc){
 volatile uint32_t *b=batch_reply();for(unsigned i=0;i<64;i++)if(i!=0&&i!=2&&i!=31&&i!=32)b[i]=0;
 b[0]=P48_BATCH_REPLY_MAGIC;b[1]=1;b[2]=ctx.nonce;b[3]=seq;b[8]=seq-1;b[9]=P49_FRAME_COUNT;b[10]=batch_source(seq);b[11]=batch_flags(seq);b[23]=ctx.listener;b[30]=saved_crc;b[P48_R_PREVIOUS_IDLE_CRC]=idle_crc;barrier();
}
static P48_FN void batch_publish(volatile uint32_t *r,unsigned phase,uint32_t error){
 volatile uint32_t *b=batch_reply();unsigned seq=batch_seq();b[63]=0;barrier();
 b[4]=phase;b[6]=phase==P48_PHASE_READY?seq-1:seq;b[7]=phase==P48_PHASE_DONE?seq:seq-1;
 b[12]=r[22];b[13]=r[23];b[14]=r[3];b[15]=r[12];b[16]=r[13];b[17]=r[14];b[18]=r[15];b[19]=r[20];b[20]=wb()[69];b[21]=cc()[69];b[22]=error;b[24]=r[8];b[25]=r[9];b[26]=r[5];b[27]=r[6];b[28]=r[7];
 if(wb()[73]==134)b[5]|=P48_REPLY_WB_VISIBILITY_WARNING;
 b[62]=p48_batch_crc((const uint32_t*)b,248);barrier();b[63]=p48_batch_token(ctx.nonce,seq,phase,b[62]);barrier();
}
static P48_FN int batch_abort(void){uint32_t a[16],b[16];volatile uint32_t *p=ptr(ctx.cap+P48_BATCH_CONTROL_OFFSET+0x200u);batch_copy(a,p,16);batch_copy(b,p,16);return batch_equal(a,b,16)&&p48_batch_abort_valid(a,ctx.nonce,batch_seq());}
static P48_STOP_FN int32_t batch_stop(unsigned seq){
 volatile uint32_t *s=ptr(ctx.cap+P39_STOP_OFFSET),*a=ptr(ctx.cap+P48_INITIAL_STOP_OFFSET);
 if(seq==1){
  for(unsigned i=0;i<64;i++)if(a[i])return 127;
  batch_reply()[P48_R_STOP_CALLS]++;int32_t rc=stop_gate();if(rc)return rc;
  if(!p39_stop_record_valid((const uint32_t*)s,ctx.nonce,ctx.cap))return 126;
  for(unsigned i=0;i<64;i++)a[i]=s[i];barrier();return 0;
 }
 uint32_t initial[64],frozen[64];batch_copy(initial,a,64);batch_copy(frozen,ptr(ctx.cap+P48_BATCH_FROZEN_OFFSET),64);
 if(!p39_stop_record_valid(initial,ctx.nonce,ctx.cap)||!p48_batch_frozen_valid(frozen,ctx.nonce)||
    frozen[P48_F_INITIAL_STOP_PA]!=ctx.cap+P48_INITIAL_STOP_OFFSET||
    frozen[P48_F_INITIAL_STOP_CRC]!=p48_batch_crc(initial,P48_INITIAL_STOP_BYTES))return 127;
 for(unsigned i=0;i<64;i++)if(initial[i]!=a[i])return 127;
 uint32_t handoff[64],again[64];batch_copy(handoff,ptr(ctx.cap+P48_IDLE_HANDOFF_OFFSET),64);batch_copy(again,ptr(ctx.cap+P48_IDLE_HANDOFF_OFFSET),64);
 if(!batch_equal(handoff,again,64)||!p48_idle_handoff_valid(handoff,ctx.nonce,seq-1)||handoff[62]!=batch_reply()[P48_R_PREVIOUS_IDLE_CRC]||handoff[5]!=initial[5]||handoff[6]!=initial[6]||handoff[7]!=frozen[P48_F_INITIAL_STOP_CRC])return 127;
 for(unsigned i=0;i<64;i++)ptr(ctx.cap+P48_PREVIOUS_IDLE_OFFSET)[i]=handoff[i];barrier();
 batch_reply()[P48_R_STOP_REUSE_CHECKS]++;
 s[0]=P39_STOP_MAGIC;s[1]=2;s[2]=ctx.nonce;s[3]=7;s[4]=P39_STOP_HOLDER;
 s[5]=initial[5];s[6]=initial[6];s[7]=P39_STOP_PATTERN;s[8]=0;
 for(unsigned i=9;i<=14;i++)s[i]=UINT32_MAX;s[19]=s[23]=s[27]=0xdeadcafeu;
 s[29]=ctx.cap+P48_INITIAL_STOP_OFFSET;s[30]=1;s[46]=seq;s[47]=p48_stop_fingerprint(initial);
 s[20]=handoff[62];s[21]=seq-1;
 for(unsigned i=0;i<14;i++){s[32+i]=*ptr(p39_stop_addresses[i]);if(s[32+i]!=handoff[8+i])return 129;}
 if(!stop_handle(initial[5],initial[6]))return 128;
 s[9]=(uint32_t)event_ref((uint32_t)(s+16));
 if(s[9]||s[17]||(s[18]&P39_STOP_PATTERN)!=P39_STOP_PATTERN||s[19]!=0xdeadcafeu)return 128;
 s[15]=idle();if(!s[15])return 129;
 s[14]=(uint32_t)event_ref((uint32_t)(s+24));s[31]=stop_handle(initial[5],initial[6]);
 for(unsigned i=0;i<14;i++){s[48+i]=*ptr(p39_stop_addresses[i]);if(s[48+i]!=handoff[8+i])return 129;}
 s[22]=1;
 if(!idle()||!p48_stop_record_valid((const uint32_t*)s,initial,ctx.nonce,ctx.cap,seq))return 128;
 batch_reply()[P48_R_FLAGS]|=P48_REPLY_STOP_REUSED;barrier();return 0;
}
static P48_STOP_FN int batch_capture_idle(unsigned seq){
 uint32_t first[64];batch_copy(first,ptr(ctx.cap+P48_INITIAL_STOP_OFFSET),64);
 if(!idle()||!stop_handle(first[5],first[6]))return 0;
 volatile uint32_t *h=ptr(ctx.cap+P48_IDLE_HANDOFF_OFFSET);for(unsigned i=0;i<64;i++)h[i]=0;
 h[0]=P48_IDLE_HANDOFF_MAGIC;h[1]=1;h[2]=ctx.nonce;h[3]=seq;h[4]=1;h[5]=first[5];h[6]=first[6];h[7]=p48_batch_crc(first,P48_INITIAL_STOP_BYTES);
 for(unsigned i=0;i<14;i++)h[8+i]=*ptr(p39_stop_addresses[i]);barrier();
 for(unsigned i=0;i<14;i++)h[22+i]=*ptr(p39_stop_addresses[i]);barrier();
 if(!idle())return 0;h[62]=p48_batch_crc((const uint32_t*)h,248);barrier();h[63]=p48_batch_token(ctx.nonce,seq,P48_IDLE_HANDOFF_MAGIC,h[62]);barrier();
 batch_copy(first,h,64);if(!p48_idle_handoff_valid(first,ctx.nonce,seq))return 0;batch_reply()[P48_R_IDLE_HANDOFF_CRC]=first[62];return 1;
}
/* The first template is captured before source IQ changes its DDR shadow.
 * Later gates verify that same immutable, nonce/initial-stop-bound record;
 * they must never attempt another first capture or replace it with source IQ. */
static P49_PD_CONTRACT_FN int batch_frozen_verify(unsigned prior_state){
 volatile uint32_t*f=ptr(ctx.cap+P48_BATCH_FROZEN_OFFSET),*s=ptr(ctx.cap+P48_INITIAL_STOP_OFFSET);
 uint32_t copy[64],initial[64];batch_copy(copy,f,64);batch_copy(initial,s,64);
 if(!p48_batch_frozen_valid(copy,ctx.nonce)||copy[P48_F_ORIGINAL_TASK_STATE]!=(prior_state&255u)||
  copy[P48_F_INITIAL_STOP_PA]!=ctx.cap+P48_INITIAL_STOP_OFFSET||
  !p39_stop_record_valid(initial,ctx.nonce,ctx.cap)||
  copy[P48_F_INITIAL_STOP_CRC]!=p48_batch_crc(initial,P48_INITIAL_STOP_BYTES))return 0;
 for(unsigned i=0;i<64;i++)if(copy[i]!=f[i]||initial[i]!=s[i])return 0;
 return 1;
}
static P48_STOP_FN int batch_freeze(unsigned seq,unsigned prior_state){
 volatile uint32_t *f=ptr(ctx.cap+P48_BATCH_FROZEN_OFFSET);uint32_t before[13],after[13],copy[64];
 if(seq!=1)return batch_frozen_verify(prior_state);
 for(unsigned i=0;i<64;i++)if(f[i])return 0;
 cc_read_roots(before);if(!p48_cc_target_roots(before)||!p48_cc_observer_roots(before)||!cc_configured())return 0;
 f[0]=P48_BATCH_FROZEN_MAGIC;f[1]=1;f[2]=ctx.nonce;f[3]=1;f[4]=before[4];f[5]=P48_CC_TARGET0;f[6]=P48_CC_TARGET1;f[7]=15;
 for(unsigned i=0;i<6;i++){f[8+i]=*ptr(P41_LCAC+4*i);f[14+i]=*ptr(P41_SRA+4*i);}
 for(unsigned i=0;i<10;i++){f[20+i]=*ptr(P48_CC_TARGET0+4*i);f[30+i]=*ptr(P48_CC_TARGET1+4*i);}barrier();
 for(unsigned i=0;i<6;i++)if(f[8+i]!=*ptr(P41_LCAC+4*i)||f[14+i]!=*ptr(P41_SRA+4*i))return 0;
 for(unsigned i=0;i<10;i++)if(f[20+i]!=*ptr(P48_CC_TARGET0+4*i)||f[30+i]!=*ptr(P48_CC_TARGET1+4*i))return 0;
 cc_read_roots(after);if(!batch_equal(before,after,13)||!p48_cc_target_roots(after)||!p48_cc_observer_roots(after))return 0;
 f[40]=prior_state&255u;f[P48_F_INITIAL_STOP_MAGIC]=0x53543850u;f[P48_F_INITIAL_STOP_PA]=ctx.cap+P48_INITIAL_STOP_OFFSET;f[P48_F_INITIAL_STOP_SEQ]=1;
 f[P48_F_INITIAL_STOP_CRC]=p48_batch_crc((const uint32_t*)(ctx.cap+P48_INITIAL_STOP_OFFSET),P48_INITIAL_STOP_BYTES);
 f[62]=p48_batch_crc((const uint32_t*)f,248);barrier();f[63]=p48_batch_token(ctx.nonce,1,P48_BATCH_FROZEN_MAGIC,f[62]);barrier();batch_copy(copy,f,64);return p48_batch_frozen_valid(copy,ctx.nonce);
}
#include "rtos_config_iq.inc"
static P48_FN int32_t fail(volatile uint32_t *r,uint32_t phase){
 /* Never restore/reset a potentially live DMA. Every failure is terminal for
  * this batch; Linux retains CAP until return/liveness or a cold restart. */
 if(!wb()[71]&&!cc()[71]){
  if(!cc_dma_cleanup()){cc_busy(1);r[3]=phase;batch_publish(r,P48_PHASE_FAILED,phase);return -1;}
  if(!gt_restore()){cc_busy(1);r[3]=phase;batch_publish(r,P48_PHASE_FAILED,phase);return -1;}
  if(wb()[65]&&!wb()[255]&&!wb_restore())wb()[73]=phase;
  if(cc()[68]&&!wb()[255])cc_restore();
  if(wb()[253]&&!wb()[255]){batch_reply()[36]++;wb()[254]=(uint32_t)native(0x801da980u,0,0,0);r[20]=wb()[254];wb()[255]=1;*(volatile unsigned char*)(P39_TASK+3364)=(unsigned char)r[29];}
 }
 barrier();r[3]=phase;batch_publish(r,P48_PHASE_FAILED,phase);barrier();return -1;
}
static void snapshot(unsigned slot,uint32_t event,int32_t status,uint32_t entry_sp){
 if(slot>=P39_DIAG_SLOTS)return;
 volatile uint32_t *d=ptr(ctx.cap+P39_DIAG_OFFSET+slot*P39_DIAG_WORDS*4u);
 d[0]=0;d[1]=slot;d[2]=ctx.nonce;d[3]=0;d[4]=event;d[5]=(uint32_t)status;d[6]=entry_sp;
 d[7]=d[8]=d[9]=d[10]=d[11]=0;
 /* All offsets refer to pinned original call frames, before our C prologue.
  * One-shot dispatcher40 + event dispatcher8 + original callback8 =56.
  * The ISR keeps its remaining pending word at localSP+4. Never scan a stack.
  * Original ISR acknowledges the MMIO bit before the original callback.
  */
 if(entry_sp>=0x81000000u&&entry_sp<0x93fff000u&&!(entry_sp&7u)){
  uint32_t dispatch_lr=*ptr(entry_sp+36),callback_lr=*ptr(entry_sp+44),isr_lr=*ptr(entry_sp+52);
  int frame=event==0&&status==0&&dispatch_lr==0x801da2f0u&&callback_lr==0x801da3a0u&&isr_lr==0x80036c28u;
  int dma=event==1&&isr_lr==0x80036d94u&&((status==0&&dispatch_lr==0x801da308u&&callback_lr==0x801da480u)||(status==-2&&dispatch_lr==0x801da2d8u&&callback_lr==0x801da4b8u));
  if(frame||dma){uint32_t bit=*ptr(entry_sp+56),pending=*ptr(entry_sp+60);unsigned want=frame?5u:status==-2?7u:5u;
   if(bit==want&&(pending&(1u<<want))){d[7]=1;d[8]=bit;d[9]=pending;d[10]=frame?0x20810418u:0x2081040cu;d[11]=frame?1u:2u;}
  }
 }
 for(unsigned i=0;i<P39_DIAG_READS;i++)d[12+i]=*ptr(p39_diag_addresses[i]);
 barrier();d[3]=1;d[0]=P39_DIAG_MAGIC;barrier();
}
__attribute__((used,visibility("hidden")))
void nxl_irq_body(uint32_t listener,uint32_t event,int32_t status,uint32_t entry_sp){
 volatile uint32_t *r=ptr(ctx.result);volatile uint32_t *b=batch_reply();
 /* Sticky across result refresh and READY/DONE. Never erase these in
  * batch_init: a late callback must poison the old handoff before reuse. */
 if(b[0]!=P48_BATCH_REPLY_MAGIC||b[2]!=ctx.nonce)return;
 uint32_t d=listener-(ctx.cap+P48_BATCH_LISTENER_OFFSET);
 if(listener!=ctx.listener){b[63]=0;if(d%P48_BATCH_LISTENER_STRIDE==0&&d/P48_BATCH_LISTENER_STRIDE<10)b[31]++;else b[32]++;barrier();return;}
 if(event>1||*ptr(listener+4)!=ctx.result||*ptr(listener+8)!=ctx.nonce||*ptr(listener+12)!=batch_seq()||r[0]!=P39_MAGIC||r[2]!=ctx.nonce||r[3]<5||r[3]>6){b[63]=0;b[32]++;barrier();return;}
 snapshot(event?7:6,event,status,entry_sp);
 unsigned i=event?14:12;r[i+1]=(uint32_t)status;r[i]++;r[21]=~ctx.nonce;barrier();
}
/* Preserve exact original entrySP while retaining the original listener ABI. */
__attribute__((used,naked,visibility("default")))
void nxl_irq(uint32_t listener,uint32_t event,int32_t status){
 __asm__ volatile("mov r3, sp\n\tb nxl_irq_body");
}
__attribute__((used,visibility("default"),section(".text.entry")))
int32_t nxl_entry(uint32_t packet){
 unsigned seq=batch_seq();volatile uint32_t *p=ptr(packet),*payload=ptr(p[4]);
 if(!seq||!p[4]||p[1]!=12||payload[0]!=p48_batch_entry_token(ctx.nonce,seq)||payload[1]!=ctx.source||payload[2]!=ctx.source_bytes)return native(ctx.original,packet,0,0);
 volatile uint32_t *r=ptr(ctx.result);uint32_t q[32],saved_crc=0,idle_crc=0;
 if(ctx.cap_bytes!=P39_CAP_BYTES||ctx.result!=ctx.cap+P39_RESULT_OFFSET||ctx.source!=ctx.cap+P39_SOURCE_OFFSET||ctx.target!=ctx.cap+P39_TARGET_OFFSET||ctx.source_bytes!=P39_SOURCE_BYTES||ctx.target_bytes!=P39_TARGET_BYTES||ctx.task!=P39_TASK||ctx.slot!=P39_SLOT||*ptr(ctx.slot)!=ctx.cap||ctx.vtable!=ctx.listener+P48_BATCH_VTABLE_DELTA||*ptr(ctx.listener+4)!=ctx.result||*ptr(ctx.listener+8)!=ctx.nonce||*ptr(ctx.listener+12)!=seq)return -1;
 *ptr(ctx.slot)=ctx.original;barrier();r[0]=P39_MAGIC;r[1]=1;r[2]=ctx.nonce;r[3]=1;
 r[24]=ctx.cap;r[25]=ctx.source;r[26]=ctx.target;r[27]=ctx.source_bytes;r[28]=ctx.target_bytes;
 if(!batch_previous(seq,&saved_crc,&idle_crc)){r[3]=201;barrier();return -1;}
 batch_init(seq,saved_crc,idle_crc);
 if(batch_reply()[31]||batch_reply()[32]||!batch_request(q,seq,1)||!batch_bind_requests(q,seq))return fail(r,200);
 batch_reply()[29]=q[30];
 r[4]=(uint32_t)cache(P39_SLOT_PAGE,4096);if((int32_t)r[4]<0)return fail(r,100);
 r[18]=idle();if(!r[18])return fail(r,101);r[29]=*ptr(P39_TASK+3364);
 barrier();r[3]=P39_READY;batch_publish(r,P48_PHASE_READY,0);
 unsigned spins=0;while(r[16]!=(ctx.nonce^P39_GO_XOR)){if(r[17]==~ctx.nonce||batch_abort())return fail(r,102);if(++spins>=50000000u)return fail(r,103);}
 if(batch_reply()[31]||batch_reply()[32]||!batch_request(q,seq,0)||!batch_bind_requests(q,seq)||batch_abort())return fail(r,202);
 batch_reply()[29]=q[30];batch_publish(r,P48_PHASE_ACK,0);
 r[30]=spins;r[19]=idle();if(!r[19])return fail(r,104);
 if(!wb_prepare())return fail(r,130);
 still_audit(1);r[3]=8;barrier();int32_t stop_status=batch_stop(seq);
 if(stop_status||!p48_stop_record_valid((const uint32_t*)(ctx.cap+P39_STOP_OFFSET),(const uint32_t*)(ctx.cap+P48_INITIAL_STOP_OFFSET),ctx.nonce,ctx.cap,seq))return fail(r,stop_status?(uint32_t)stop_status:126u);
 batch_reply()[5]|=P48_REPLY_STOP_VERIFIED;
 r[5]=(uint32_t)cache(ctx.source,ctx.source_bytes);r[6]=(uint32_t)cache(ctx.target,ctx.target_bytes);
 if((int32_t)r[5]<0||(int32_t)r[6]<0)return fail(r,105);
 #ifdef NXL_FAST_PIPELINE
 r[22]=q[P48_Q_SOURCE_FNV];
#else
 r[22]=hash(ctx.source,ctx.source_bytes);
#endif
 if(r[22]!=q[P48_Q_SOURCE_FNV])return fail(r,203);
 still_audit(2);isp_audit(1);snapshot(0,0xffffffffu,0,0);r[3]=3;barrier();
 volatile uint32_t *in=ptr(0x817d7b48u),*out=ptr(0x817d7b48u+72);
 for(unsigned v=0;v<P39_CONFIG_VARIANTS;v++){
  wb()[253]=1;batch_reply()[34]++;barrier();r[8]=(uint32_t)config_source_iq(ctx.job+v*32u,r[29]);r[3]=4;snapshot(1+v,0xffffffffu,(int32_t)r[8],0);
  volatile uint32_t *d=ptr(ctx.cap+P39_DIAG_OFFSET+(1+v)*512u);
  if((int32_t)r[8]||in[0]!=ctx.source||in[2]!=P39_SOURCE_STRIDE||out[0]!=ctx.target||out[1]!=ctx.target+P39_UV_OFFSET||out[2]!=P39_TARGET_STRIDE||(out[3]&0x7f3u)!=0x41u||d[62]!=in[0]||d[64]!=in[2]||d[65]!=in[3]||d[71]!=out[0]||d[72]!=out[1]||d[73]!=out[2]||d[74]!=out[3])return fail(r,106);
 }
 still_audit(3);isp_audit(2);for(unsigned i=0;i<9;i++){r[32+i]=in[i];r[41+i]=out[i];}
 uint32_t owner=*ptr(0x81543d40u);
#ifdef NXL_COLD_INIT_TEST
 /* SetConfig must have constructed the real owner. Never synthesize byte1.
  * Check both the original selected child and task state before any Start. */
 if(!configuration_ready(owner))return fail(r,144);
#endif
 for(unsigned i=0;i<14;i++)r[50+i]=ptr(owner)[i];
 if((r[51]&15u)!=5u||r[53]!=P39_W||r[54]!=P39_H||r[57]!=P39_TILE_W||r[58]!=P39_TILE_H||r[59]!=14||r[60]!=3)return fail(r,106);
 if(!batch_frozen_verify(r[29]))return fail(r,204);
 volatile uint32_t *f=ptr(ctx.cap+P48_BATCH_FROZEN_OFFSET);
 (void)f;if(!(q[7]&2u)||!p49_pd_record_single())return fail(r,143);
 if(!wb_apply(q[7]&1u,f+P48_F_WB_PRIMARY))return fail(r,130);
 r[10]=(uint32_t)native(0x801c9e0cu,P39_TASK,0,ctx.listener);r[11]=(uint32_t)native(0x801c9e0cu,P39_TASK,1,ctx.listener);if(r[10]||r[11])return fail(r,107);
 if(batch_abort())return fail(r,102);
 if(batch_reply()[31]||batch_reply()[32])return fail(r,205);
 snapshot(5,0xffffffffu,0,0);wb_snapshot(112);wb()[76]=wb_target_matches(112);if(!cc_snapshot(P48_CC_PRESTART)||!cc_stage(P48_CC_PRESTART)->target_equal)return fail(r,142);barrier();
 still_audit(4);isp_audit(3);wb()[71]=1;cc_busy(1);r[3]=5;batch_reply()[35]++;batch_publish(r,P48_PHASE_INFLIGHT,0);
 r[9]=(uint32_t)native(0x801d9f20u,P39_TASK,0,0);if((int32_t)r[9])return fail(r,108);
 wb_snapshot(228);wb()[78]=wb_target_matches(228);cc_snapshot(P48_CC_POSTSTART);gt_observe(GT_AFTER_START);isp_audit(4);barrier();r[3]=6;barrier();spins=0;
 while(!r[12]||!r[14]){if(++spins>=50000000u){r[31]=spins;snapshot(8,0xffffffffu,109,0);return fail(r,109);}}
 r[31]=spins;snapshot(8,0xffffffffu,0,0);
 if(r[12]!=1||r[14]!=1||(int32_t)r[13]||(int32_t)r[15]||r[21]!=~ctx.nonce)return fail(r,110);
 volatile uint32_t *frame=ptr(ctx.cap+P39_DIAG_OFFSET+6*512u),*dma=ptr(ctx.cap+P39_DIAG_OFFSET+7*512u);
 if(!frame[7]||!dma[7]||(dma[9]&(1u<<7)))return fail(r,113);
 wb()[71]=0;cc_busy(0);still_audit(5);isp_audit(5);gt_observe(GT_AFTER_COMPLETE);wb_snapshot(128);wb()[77]=wb_target_matches(128);wb()[79]=wb_secondary_stable();cc_snapshot(P48_CC_POSTCOMPLETE);batch_reply()[5]|=P48_REPLY_TASK_COMPLETED;barrier();
 if(batch_abort())return fail(r,102);
 r[7]=(uint32_t)cache(ctx.target,ctx.target_bytes);if((int32_t)r[7]<0)return fail(r,111);
#ifdef NXL_FAST_PIPELINE
 r[23]=0;
#else
 r[23]=hash(ctx.target,ctx.target_bytes);
#endif
 if(!gt_restore())return fail(r,145);if(!wb_restore())return fail(r,135);if(!cc_restore())return fail(r,141);
 isp_audit(6);
 batch_reply()[36]++;r[20]=(uint32_t)native(0x801da980u,0,0,0);wb()[254]=r[20];wb()[255]=1;barrier();
 *(volatile unsigned char*)(P39_TASK+3364)=(unsigned char)r[29];barrier();cc_snapshot(P48_CC_POSTCLEANUP);still_audit(6);
 if(r[20]||*ptr(P39_TASK+3284)||*ptr(P39_TASK+3288))return fail(r,112);
 if(!batch_capture_idle(seq))return fail(r,206);
 batch_reply()[5]|=P48_REPLY_CLEANUP_VERIFIED;wb()[64]=7;barrier();r[3]=P39_DONE;batch_publish(r,P48_PHASE_DONE,0);return 0;
}
