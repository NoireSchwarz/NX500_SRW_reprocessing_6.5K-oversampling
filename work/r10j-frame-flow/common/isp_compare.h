/* SPDX-License-Identifier: AGPL-3.0-only. Fixed NX500 1.12 observation ABI.
 * These DDR parameter shadows are not evidence of SRAM DMA consumption.
 * Reads are bounded to known banks and WB registers, never image pointers.
 */
#ifndef NX_ISP_COMPARE_H
#define NX_ISP_COMPARE_H
#include <stdint.h>
#define IC_MAGIC 0x31435049u
#define IC_BANK_WORDS (5888u/4u)
#define IC_CAP_OFFSET 0x03e00000u
#define IC_STAGES 6u
struct ic_sample {
 uint32_t magic,schema,nonce,stage,mask,stable,pte_before,pte_after;
 uint32_t roots_before[4],roots_after[4];
 uint32_t wb[18],bank[2][IC_BANK_WORDS],checksum,commit;
};
typedef int (*ic_reader)(void *,uint32_t,void *,unsigned);
static inline uint32_t ic_hash(const void *p,unsigned n){const unsigned char *b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static inline int ic_pte(uint32_t d){
 if((d&3u)!=2u||(d&0x0004e010u)||!(d&0x400u)||(d&0xfff00000u)!=0xbb700000u)return 0;
 unsigned n=((d>>10)&4u)|((d>>2)&3u);return ((0x000a8aa4u>>(2*n))&3u)==2u&&((0x44e048e0u>>(2*n))&3u)==0u&&((0x44e048e0u>>(16+2*n))&3u)==0u;
}
static inline int ic_roots(ic_reader read,void *ctx,uint32_t out[4]){
 return read(ctx,0x8153e6d4u,out,4)&&read(ctx,0x8153e6d4u+3364u,out+1,4)&&read(ctx,0x81543d40u,out+2,4)&&read(ctx,0x8153e6d4u+3428u,out+3,4);
}
static inline int ic_capture(ic_reader read,void *ctx,struct ic_sample *s,uint32_t nonce,unsigned stage){
 uint32_t block[128];s->magic=0;s->schema=1;s->nonce=nonce;s->stage=stage;s->mask=s->stable=0;s->pte_before=s->pte_after=0;
 for(unsigned i=0;i<4;i++)s->roots_before[i]=s->roots_after[i]=0;
 for(unsigned i=0;i<18;i++)s->wb[i]=0;
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<IC_BANK_WORDS;i++)s->bank[k][i]=0;
 if(!ic_roots(read,ctx,s->roots_before)||s->roots_before[0]!=0x80592640u)goto finish;
 if(read(ctx,0x20810570u,s->wb,24))s->mask|=1u;
 if(read(ctx,0x20810770u,s->wb+6,24))s->mask|=2u;
 if(read(ctx,0x208107d4u,s->wb+12,24))s->mask|=4u;
 if(read(ctx,0x81002edcu,&s->pte_before,4)&&ic_pte(s->pte_before)){
  for(unsigned k=0;k<2;k++){
   uint32_t base=k?0xbb71e000u:0xbb71c000u;int ok=1;
   for(unsigned i=0;i<IC_BANK_WORDS;i+=128){unsigned n=IC_BANK_WORDS-i;if(n>128)n=128;if(!read(ctx,base+i*4,s->bank[k]+i,n*4)){ok=0;break;}}
   if(!ok)continue;
   /* Re-read the whole fixed bank: concurrent updates remain distinguishable
    * from a consistent sample. No retry can stall a camera operation. */
   for(unsigned i=0;i<IC_BANK_WORDS;i+=128){unsigned n=IC_BANK_WORDS-i;if(n>128)n=128;if(!read(ctx,base+i*4,block,n*4)){ok=0;break;}for(unsigned j=0;j<n;j++)if(block[j]!=s->bank[k][i+j])ok=0;}
   if(ok)s->mask|=8u<<k;
  }
 }
 if(read(ctx,0x81002edcu,&s->pte_after,4)&&ic_roots(read,ctx,s->roots_after)){
  s->stable=s->pte_before==s->pte_after;
  for(unsigned i=0;i<4;i++)if(s->roots_before[i]!=s->roots_after[i])s->stable=0;
  const uint32_t wb_at[3]={0x20810570u,0x20810770u,0x208107d4u};
  for(unsigned k=0;k<3;k++){if(!read(ctx,wb_at[k],block,24))s->stable=0;else for(unsigned i=0;i<6;i++)if(block[i]!=s->wb[k*6+i])s->stable=0;}
 }
finish:
 s->magic=IC_MAGIC;s->checksum=ic_hash(s,sizeof *s-8u);s->commit=IC_MAGIC^nonce^stage^s->checksum;return s->stable&&s->mask==31u;
}
static inline int ic_valid(const struct ic_sample *s,uint32_t nonce,unsigned stage){return s->magic==IC_MAGIC&&s->schema==1&&s->nonce==nonce&&s->stage==stage&&s->mask<=31u&&s->stable<=1u&&s->checksum==ic_hash(s,sizeof *s-8u)&&s->commit==(IC_MAGIC^nonce^stage^s->checksum);}
_Static_assert(sizeof(struct ic_sample)*IC_STAGES<=0x20000u,"bounded control audit");
#endif
