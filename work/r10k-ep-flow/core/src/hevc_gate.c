/* Freestanding PIC, no allocator, log, file, codec or target calls.
 * The only syscall is CLOCK_MONOTONIC while a committed command is enabled.
 * Invalid commands/identities/leases always pass the frame to OEM ProcessData.
 */
#include "../../common/video_hevc_gate.h"
static void fence(void){__asm__ volatile("dmb ish":::"memory");}
static uint32_t load32(uint32_t a){return *(const volatile uint32_t*)(uintptr_t)a;}
static int user(uint32_t a,unsigned n){return a>=0x10000u&&a<0xbfff0000u&&n<=0xbfff0000u-a&&!(a&3u);}
static int milliseconds(uint32_t*out){
 struct {int32_t sec,nsec;}t;
 register uint32_t r0 __asm__("r0")=1u;
 register uint32_t r1 __asm__("r1")=(uint32_t)(uintptr_t)&t;
 register uint32_t r7 __asm__("r7")=263u;
 __asm__ volatile("svc 0":"+r"(r0):"r"(r1),"r"(r7):"r2","r3","r12","lr","cc","memory");
 if(r0||t.sec<0||t.nsec<0||t.nsec>=1000000000)return 0;
 *out=(uint32_t)t.sec*1000u+(uint32_t)t.nsec/1000000u;return 1;
}
__attribute__((visibility("hidden"))) int hg_decide(const uint32_t*r,uint32_t sp,uint32_t base){
 volatile uint32_t*page=(volatile uint32_t*)(uintptr_t)(base+HG_STATE);
 volatile struct hg_command*src=(volatile struct hg_command*)((uintptr_t)page+HG_COMMAND);
 uint32_t seq=src->seq;
 if(!seq||(seq&1u)||!src->enabled)return 0;
 struct hg_command cmd;uint32_t*w=(uint32_t*)&cmd;
 fence();for(unsigned i=0;i<16;i++)w[i]=((volatile uint32_t*)src)[i];fence();
 if(src->seq!=seq||cmd.seq!=seq||cmd.schema!=HG_SCHEMA||!cmd.cookie||cmd.enabled!=1u||cmd.reserved||
    hg_hash(w+1,14)!=cmd.hash||cmd.a_end_ms-cmd.start_ms!=HG_PHASE_MS||
    cmd.b_end_ms-cmd.a_end_ms!=HG_PHASE_MS||cmd.end_ms-cmd.b_end_ms!=HG_PHASE_MS)return 0;
 /* The library dispatch is shared. Legitimate other encoder objects must
  * pass untouched and must not invalidate the selected stream's guard. */
 if(r[0]!=cmd.encoder){__atomic_fetch_add(page+34,1u,__ATOMIC_RELAXED);__atomic_store_n(page+35,r[0],__ATOMIC_RELAXED);return 0;}
 volatile uint32_t*busy=page+32;
 if(__atomic_exchange_n(busy,1u,__ATOMIC_ACQUIRE)){__atomic_fetch_add(page+33,1u,__ATOMIC_RELAXED);return 0;}
 volatile struct hg_stats*s=(volatile struct hg_stats*)((uintptr_t)page+HG_STATS);
 uint32_t next=s->seq+1u;if(!(next&1u))next++;s->seq=next;fence();
 if(s->cookie!=cmd.cookie){
  for(unsigned i=1;i<HG_STATS_WORDS;i++)((volatile uint32_t*)s)[i]=0;
  s->cookie=cmd.cookie;s->encoder=cmd.encoder;s->queue=cmd.queue;
 }
 s->calls++;s->last_flags=load32(sp+16u);
 int drop=0;uint32_t now=0;
 if(!milliseconds(&now)){s->clock_failed++;goto done;}
 s->last_ms=now;
 if(!hg_time_ge(cmd.lease_ms,now)||cmd.lease_ms-now>HG_LEASE_MS){s->expired++;goto done;}
 if(!hg_time_ge(now,cmd.start_ms)||hg_time_ge(now,cmd.end_ms)){s->phase=hg_time_ge(now,cmd.end_ms)?4u:0u;s->inactive++;goto done;}
 s->reserved[0]=0;s->reserved[1]=r[0];s->reserved[2]=r[4];s->reserved[3]=r[5];s->reserved[4]=r[6];s->reserved[5]=r[13];s->reserved[6]=sp;
 if(r[0]!=cmd.encoder||r[4]!=r[0]||r[5]!=cmd.queue||r[6]!=sp+0x58u||r[13]!=base+HG_CALL+4u||!user(r[0],0x120u)||!user(r[5],0x40u)){s->reserved[0]=1;s->bad_guard++;goto done;}
 s->reserved[7]=load32(r[0]);s->reserved[8]=load32(r[5]);
 if(s->reserved[7]!=base+0x59da8u||s->reserved[8]!=base+0x593b0u){s->reserved[0]=2;s->bad_guard++;goto done;}
 s->reserved[9]=load32(r[6]+24u);
 if(s->reserved[9]!=s->last_flags){s->reserved[0]=3;s->bad_guard++;goto done;}
 {uint32_t port=load32(r[0]+0x2cu);s->reserved[10]=port;
 if(!user(port,12u)){s->reserved[0]=4;s->bad_guard++;goto done;}
 s->reserved[11]=load32(port+4u);if(s->reserved[11]!=r[5]){s->reserved[0]=5;s->bad_guard++;goto done;}}
 {unsigned p=hg_time_ge(now,cmd.b_end_ms)?2u:hg_time_ge(now,cmd.a_end_ms)?1u:0u;
  s->phase=p+1u;s->seen[p]++;if(!s->first_ms[p])s->first_ms[p]=now;s->last_phase_ms[p]=now;
  uint32_t flags=s->last_flags;
  if(!(flags&8u)||(flags&0x31u))s->control[p]++;
  else if(p==1u){drop=(s->cursor++&3u)!=0u;}
  if(drop)s->dropped[p]++;else s->passed[p]++;
 }
 done:fence();s->seq=next+1u;__atomic_store_n(busy,0u,__ATOMIC_RELEASE);return drop;
}
