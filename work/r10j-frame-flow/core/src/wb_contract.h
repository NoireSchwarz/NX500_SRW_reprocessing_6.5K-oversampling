/* SPDX-License-Identifier: AGPL-3.0-only. Nonce-bound SRW WB request and audit. */
#ifndef P41_WB_CONTRACT_H
#define P41_WB_CONTRACT_H
#include <stdint.h>
#include "wb_native_packet.h"
#include "black_level_contract.h"
#define P41_OFFSET 0xa000u
#define P41_BYTES 1024u
#define P41_MAGIC 0x42573134u
#define P49_WB_REQUEST_SCHEMA 3u
#define P49_WB_AUDIT_SIGNATURE 0x57423450u
#define P49_WB_TARGET_WORD 160u
#define P41_GAIN_MASK 0x7ff00000u
#define P41_LCAC 0x20810770u
#define P41_SRA 0x208107d4u
static inline uint32_t p41_checksum(const uint32_t *v,unsigned words){
 uint32_t h=2166136261u;for(unsigned i=0;i<words;i++)for(unsigned b=0;b<4;b++)h=(h^((v[i]>>(8*b))&255u))*16777619u;return h;
}
static inline int p41_request_valid(const uint32_t *v,uint32_t nonce){
 if(v[0]!=P41_MAGIC||v[1]!=P49_WB_REQUEST_SCHEMA||v[2]!=nonce||v[3]!=1||!v[4]||v[4]>25600||v[48]>63||v[49]>1||v[50]>1||v[63]!=p41_checksum(v,63))return 0;
 for(unsigned i=0;i<4;i++)if(v[8+i]<64||v[8+i]>2047||v[12+i]<256||v[12+i]>65536)return 0;
 for(unsigned i=0;i<4;i++){uint32_t n=v[12+i]*256u,g=v[13];if(v[8+i]*g>n||(v[8+i]+1u)*g<=n)return 0;}
 if(v[53]<v[5]||v[54]!=P49_WB_C0_MAGIC||v[55]!=P49_WB_C0_REVISION||v[56]!=P49_WB_C0_BYTES||
    v[57]!=p49_wb_selector(v[51])||v[58]!=p49_wb_flag(v[51])||v[59]!=0u||v[60]!=1u||v[61]!=1u||!v[62]||v[62]!=p50_black_profile(v+16,v[51]))return 0;
 return p49_wb_word_valid(v[51],v[52],v+8);
}
static inline uint32_t p41_gain_word(uint32_t original,uint32_t q8){return (original&~P41_GAIN_MASK)|((q8<<20)&P41_GAIN_MASK);}
/* request:47C0-byteFNV,48frame,49WBmode,50CCmode,51completeC0word520,
 *52C0code56,53fileoffset,54..56C0header,57selector,58flag,59mode0,60CS1,
 *61native-no-dynamic-calibration=1,62A028-black-profile(1/2),63requestFNV. No case-count coupling.
 * audit160..165 immutable target6words,166targetFNV,167profile,168selector,
 *169flag,170mode,171CS,172dynamic-calibration-applied=0,173fullword,174code,
 *175sourceWBmode;176..187A028 binding/header audit;188..218zero. Old59word optionalIQ copy is retired in v2. */
static inline int p49_wb_target(const uint32_t *v,uint32_t out[6]){
 if(v[49])return p50_wb_black_packet(out,v+80,v[51],v[52],v+16);
 for(unsigned i=0;i<6;i++)out[i]=v[80+i];return 1;
}
static inline int p49_wb_six_equal(const uint32_t *a,const uint32_t *b){for(unsigned i=0;i<6;i++)if(a[i]!=b[i])return 0;return 1;}
static inline int p49_wb_target_audit_valid(const uint32_t *v){
 uint32_t expected[6];if(!p49_wb_target(v,expected)||!p49_wb_six_equal(expected,v+P49_WB_TARGET_WORD)||
  v[166]!=p41_checksum(expected,6)||v[167]!=P49_WB_PROFILE||v[168]!=v[57]||v[169]!=v[58]||
  v[170]!=v[59]||v[171]!=v[60]||v[172]||v[173]!=v[51]||v[174]!=v[52]||v[175]!=v[49])return 0;
 if(v[176]!=P50_BLACK_AUDIT_SIGNATURE||v[177]!=P50_BLACK_AUDIT_SCHEMA||v[182]!=v[62]||v[187]!=v[49])return 0;
 for(unsigned i=0;i<4;i++)if(v[178+i]!=v[16+i]||v[183+i]!=((expected[i/2]>>((i&1u)*16u))&65535u))return 0;
 for(unsigned i=188;i<219;i++)if(v[i])return 0;return 1;
}
static inline int p41_audit_valid(const uint32_t *v,uint32_t nonce){
 /* P48 always submits/restores the fixed native template. Equality is
  * visibility evidence; only native errors gate this audit predicate.
  *219 sourceWB mode,220 apply-called,221 restore-called,222 fixed-template. */
 if(!p41_request_valid(v,nonce)||!p49_wb_target_audit_valid(v)||v[64]!=7||v[65]!=1||v[219]!=v[49]||v[220]!=1||v[221]!=1||v[222]!=1||v[67]||v[69]||v[68]||v[70]||v[71]||v[72]||v[74]!=P49_WB_AUDIT_SIGNATURE||v[252]!=1||v[253]!=1||v[254]||v[255]!=1)return 0;
 for(unsigned i=75;i<=79;i++)if(v[i]>1)return 0;
 if(v[75]!=(unsigned)p49_wb_six_equal(v+96,v+160)||v[76]!=(unsigned)p49_wb_six_equal(v+112,v+160)||
    v[77]!=(unsigned)p49_wb_six_equal(v+128,v+160)||v[78]!=(unsigned)p49_wb_six_equal(v+228,v+160)||
    v[66]!=(unsigned)p49_wb_six_equal(v+144,v+80)||v[73]!=(v[66]?0u:134u))return 0;
 unsigned secondary=1;const unsigned slots[4]={96,112,128,228};
 for(unsigned k=0;k<4;k++)for(unsigned i=0;i<6;i++)if(v[slots[k]+6+i]!=v[86+i])secondary=0;
 return v[79]==secondary;
}
static inline int p43_wb_verified(const uint32_t *v,uint32_t nonce){
 if(!p41_audit_valid(v,nonce)||!v[49]||v[67]||v[69]||v[66]!=1||v[73]||!v[75]||!v[76]||!v[77]||!v[78]||!v[79])return 0;
 for(unsigned j=0;j<6;j++){
  uint32_t want=v[P49_WB_TARGET_WORD+j];
  if(v[96+j]!=want||v[112+j]!=want||v[128+j]!=want||v[228+j]!=want||v[144+j]!=v[80+j])return 0;
 }
 return 1;
}
#endif
