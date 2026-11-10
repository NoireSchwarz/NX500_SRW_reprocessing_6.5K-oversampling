/* SPDX-License-Identifier: AGPL-3.0-only.
 * P48 source-specific CC coefficient request and readback audit.
 * No black-level or other parameter fields are written. A DDR readback match
 * does not prove task adoption, cache coherence or hardware consumption.
 */
#ifndef P48_CC_CONTRACT_H
#define P48_CC_CONTRACT_H
#include <stdint.h>
#include "param_dma_contract.h"
#define P48_CC_OFFSET 0xa400u
#define P48_CC_BYTES 4096u
#define P48_CC_WORDS (P48_CC_BYTES/4u)
#define P48_CC_MAGIC 0x43433438u
#define P48_CC_SCHEMA 1u
#define P48_CC_ROOT 0x813ebf4cu
#define P48_CC_ENTRY 0x8017cf84u
#define P48_CC_BANK 1u
#define P48_CC_FIXED_BASE0 0xbb71b300u
#define P48_CC_FIXED_ORIGIN0 0xbb71c000u
#define P48_CC_FIXED_BASE1 0xbb71d300u
#define P48_CC_FIXED_ORIGIN1 0xbb71e000u
#define P48_CC_TARGET0 0xbb71d200u
#define P48_CC_TARGET1 0xbb71f200u
#define P48_CC_COEFF_MASK 0x7ffu
#define P48_CC_COEFF_BITS 198u
#define P48_CC_REQUEST_WORDS 64u
#define P48_CC_SNAPSHOT_WORDS 74u
#define P48_CC_SNAPSHOT_START 100u
#define P48_CC_SNAPSHOTS 7u
#define P48_CC_TEMPLATE_WORD 80u
#define P48_CC_TARGET_WORD 90u

/* Request words0..63, preserved by the RTOS module:
 *0magic,1schema,2nonce,3enabled(0/1),4source_index,5source_ISO,
 *6..8 C0 observed header(magic/revision/bytes),9..26 eighteen signed32
 * values sign-extended from the two consecutive C0 nine-s16 arrays,
 *27frame sequence1..10,28..62zero reserved,63little-endian-word FNV32.
 * Values are the native row representation, not reconstructed RGB matrices.
 * They are never normalized or inferred from A030. Row gain128 is common,
 * but the patch operation preserves each supplied signed11 field literally.
 */
struct p48_cc_request {
 uint32_t magic,schema,nonce,enabled,source_index,iso,c0_magic,c0_revision,c0_bytes;
 int32_t coefficients[18];uint32_t sequence,reserved[35],checksum;
};
_Static_assert(sizeof(struct p48_cc_request)==256u,"P48 CC request extent");

enum p48_cc_stage {P48_CC_TEMPLATE=0,P48_CC_POSTSETTER=1,
 P48_CC_PRESTART=2,P48_CC_POSTSTART=3,P48_CC_POSTCOMPLETE=4,
 P48_CC_RESTORED=5,P48_CC_POSTCLEANUP=6};
enum p48_cc_error {P48_CC_OK=0,P48_CC_BAD_REQUEST=1,P48_CC_BAD_PHASE=2,
 P48_CC_BAD_ROOTS=3,P48_CC_TEMPLATE_RACED=4,P48_CC_SETTER_FAILED=5,
 P48_CC_TARGET_MISMATCH=6,P48_CC_RESTORE_WHILE_BUSY=7,
 P48_CC_RESTORE_FAILED=8,P48_CC_BANK0_CHANGED=9};

/* Runtime/audit words:
 *64phase(0initial,1prepared,2setter-called,3restored,4disabled),65saved,
 *66restored readback match,67apply status,68apply-called,69restore status,
 *70restore-called,71in-flight,72restore-refused,73error,74audit signature,
 *75prepared,76postsetter target match,77bank0 unchanged after setter,
 *78snapshot bitmask,79coefficient bits actually changed.
 *80..89 complete original40B,90..99 complete target40B.
 *100..617 seven74word snapshots;618..1023zero reserved.
 * Mode0 explicitly submits the fixed native40B template; mode1 submits the
 * same fixed template with only198 sourceCC coefficient bits replaced.
 * Every frame therefore has the same setter/restore call sequence.
 */
struct p48_cc_snapshot {
 uint32_t valid,roots_valid,roots_equal,bank0_valid,bank0_equal;
 uint32_t bank1_valid,bank1_equal,target_equal;
 uint32_t roots_before[13],roots_after[13];
 uint32_t bank0_first[10],bank0_second[10],bank1_first[10],bank1_second[10];
};
_Static_assert(sizeof(struct p48_cc_snapshot)==P48_CC_SNAPSHOT_WORDS*4u,"P48 CC snapshot extent");
_Static_assert(P48_CC_SNAPSHOT_START+P48_CC_SNAPSHOT_WORDS*P48_CC_SNAPSHOTS<=P48_CC_WORDS,"P48 CC audit budget");
_Static_assert(P48_CC_OFFSET+P48_CC_BYTES<=0x10000u,"P48 CC before pixel source");

static inline uint32_t p48_cc_checksum(const uint32_t *v,unsigned n){
 uint32_t h=2166136261u;for(unsigned i=0;i<n;i++)for(unsigned b=0;b<4;b++)h=(h^((v[i]>>(8u*b))&255u))*16777619u;return h;
}
static inline int p48_cc_coefficients_valid(const int32_t *v){
 for(unsigned i=0;i<18;i++)if(v[i]<-1024||v[i]>1023)return 0;
 /* Q7-like row totals are carried as the first field of every triple;
  * do not accept negative row gains, but do not force a gain of128. */
 for(unsigned i=0;i<18;i+=3)if(v[i]<0)return 0;
 return 1;
}
static inline int p48_cc_request_valid(const uint32_t *w,uint32_t nonce){
 if(w[0]!=P48_CC_MAGIC||w[1]!=P48_CC_SCHEMA||!nonce||w[2]!=nonce||w[3]>1u||!w[5]||w[5]>25600u||w[63]!=p48_cc_checksum(w,63))return 0;
 if(!w[27]||w[27]>10u)return 0;for(unsigned i=28;i<63;i++)if(w[i])return 0;
 if(!w[3])return 1;
 if(w[6]!=0x433f2149u||w[7]!=20150126u||w[8]!=2048u)return 0;
 return p48_cc_coefficients_valid((const int32_t*)(w+9));
}
static inline int p48_cc_make_request(uint32_t out[64],uint32_t nonce,unsigned sequence,unsigned source_id,unsigned iso,unsigned enabled,const int32_t *coefficients){
 for(unsigned i=0;i<64;i++)out[i]=0;out[0]=P48_CC_MAGIC;out[1]=P48_CC_SCHEMA;out[2]=nonce;out[3]=enabled;out[4]=source_id;out[5]=iso;out[27]=sequence;
 if(enabled){if(!coefficients)return 0;out[6]=0x433f2149u;out[7]=20150126u;out[8]=2048u;for(unsigned i=0;i<18;i++)out[9+i]=(uint32_t)coefficients[i];}
 out[63]=p48_cc_checksum(out,63);return p48_cc_request_valid(out,nonce);
}
static inline uint32_t p48_cc_word_mask(unsigned i){return i%5u==4u?0x000007ffu:0x07ff07ffu;}
static inline void p48_cc_patch(uint32_t *out,const uint32_t *original,const int32_t *coefficients){
 for(unsigned i=0;i<10;i++)out[i]=original[i];
 for(unsigned group=0;group<2;group++)for(unsigned field=0;field<9;field++){
  unsigned half=group*10u+field,word=half/2u,shift=(half&1u)*16u;
  uint32_t mask=P48_CC_COEFF_MASK<<shift;
  out[word]=(out[word]&~mask)|(((uint32_t)coefficients[group*9u+field]&P48_CC_COEFF_MASK)<<shift);
 }
}
static inline int p48_cc_unassigned_preserved(const uint32_t *a,const uint32_t *b){
 for(unsigned i=0;i<10;i++)if((a[i]^b[i])&~p48_cc_word_mask(i))return 0;return 1;
}
static inline int p48_cc_target_roots(const uint32_t *r){
 return r[2]==P48_CC_FIXED_BASE1&&r[10]==P48_CC_FIXED_ORIGIN1&&r[4]==0x20810000u&&
  (uint64_t)r[2]+0xd00u==r[10]&&(uint64_t)r[2]+0x1f00u==P48_CC_TARGET1;
}
static inline int p48_cc_observer_roots(const uint32_t *r){
 return r[1]==P48_CC_FIXED_BASE0&&r[9]==P48_CC_FIXED_ORIGIN0&&
  (uint64_t)r[1]+0x1f00u==P48_CC_TARGET0;
}
/* Completed audit transport/readback predicate only. It never proves that
 * an ISP task adopted this DDR bank or consumed the supplied coefficients. */
static inline int p48_cc_same(const uint32_t *a,const uint32_t *b,unsigned n){for(unsigned i=0;i<n;i++)if(a[i]!=b[i])return 0;return 1;}
/* Recompute serialized read status; zero payload is never silently accepted
 * as a measured zero image. A before-root rejection has only roots_before. */
static inline int p48_cc_snapshot_record_valid(const uint32_t *v,const uint32_t *target){
 int before=p48_cc_target_roots(v+8),observe0=p48_cc_observer_roots(v+8);
 if(!before){for(unsigned i=0;i<74;i++)if((i<8||i>=21)&&v[i])return 0;return 1;}
 unsigned rv=p48_cc_target_roots(v+21),re=p48_cc_same(v+8,v+21,13),b1e=p48_cc_same(v+54,v+64,10),b0v=observe0&&p48_cc_observer_roots(v+21),b0e=b0v&&p48_cc_same(v+34,v+44,10);
 unsigned flags[8]={rv&&b1e,rv,re,b0v,b0e,rv,b1e,rv&&b1e&&p48_cc_same(v+64,target,10)};
 for(unsigned i=0;i<8;i++)if(v[i]!=flags[i])return 0;
 if(!observe0)for(unsigned i=34;i<54;i++)if(v[i])return 0;return 1;
}
static inline int p48_cc_snapshot_valid(const uint32_t *v,const uint32_t *target){
 if(!p48_cc_snapshot_record_valid(v,target))return 0;
 for(unsigned i=0;i<7;i++)if(v[i]!=1)return 0;return 1;
}
static inline int p48_cc_audit_valid(const uint32_t *w,uint32_t nonce){
 if(!p48_cc_request_valid(w,nonce)||w[64]!=3||w[65]!=1||w[66]!=1||w[67]||w[68]!=1||w[69]||w[70]!=1||w[71]||w[72]||(w[73]!=0&&w[73]!=P48_CC_BAD_ROOTS)||w[74]!=P48_CC_MAGIC||w[75]!=1||w[76]!=1||w[77]!=1||w[78]!=127||w[79]>1)return 0;
 uint32_t target[10],zero[10]={0};int32_t fields[18];for(unsigned i=0;i<18;i++)fields[i]=(int32_t)w[9+i];
 if(w[3])p48_cc_patch(target,w+80,fields);else for(unsigned i=0;i<10;i++)target[i]=w[80+i];
 if(!p48_cc_same(target,w+90,10)||!p48_cc_unassigned_preserved(w+80,w+90)||w[79]!=(unsigned)!p48_cc_same(w+80,w+90,10))return 0;
 unsigned observational_root_rejection=0;
 for(unsigned stage=0;stage<P48_CC_SNAPSHOTS;stage++){
  const uint32_t *v=w+100+stage*74;const uint32_t *expected=stage?target:zero;
  if(!p48_cc_snapshot_record_valid(v,expected))return 0;
  if(stage==0||stage==1||stage==2||stage==5){if(!p48_cc_snapshot_valid(v,expected))return 0;}
  else if(!p48_cc_target_roots(v+8))observational_root_rejection=1;
 }
 if(w[73]==P48_CC_BAD_ROOTS&&!observational_root_rejection)return 0;
 const uint32_t *templ=w+100,*post=templ+74,*pre=post+74,*restored=templ+5*74;
 if(!post[7]||!pre[7]||!p48_cc_same(templ+44,post+44,10)||!p48_cc_same(restored+64,w+80,10))return 0;
 for(unsigned i=618;i<P48_CC_WORDS;i++)if(w[i])return 0;return 1;
}
#endif
