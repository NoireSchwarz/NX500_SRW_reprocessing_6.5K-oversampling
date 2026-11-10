/* SPDX-License-Identifier: AGPL-3.0-only.
 * Bind SRW A028 RGGB to the two observed original mode0 WB-offset profiles.
 * This is an explicit metadata binding, not an additional Bayer subtraction.
 * Equal128/selector0 and equal512/selector1 are the only observed profiles.
 * Arbitrary/asymmetric black levels and selector2 have no verified mapping.
 */
#ifndef NXL_BLACK_LEVEL_CONTRACT_H
#define NXL_BLACK_LEVEL_CONTRACT_H
#include "wb_native_packet.h"
#define P50_BLACK_AUDIT_SIGNATURE 0x424c4331u
#define P50_BLACK_AUDIT_SCHEMA 1u
static inline unsigned p50_black_profile(const uint32_t black[4],uint32_t word){
 unsigned selector=p49_wb_selector(word);uint32_t level=selector==0u?128u:selector==1u?512u:0u;
 if(!black||!level)return 0;
 for(unsigned i=0;i<4;i++)if(black[i]!=level)return 0;
 return selector+1u;
}
static inline int p50_wb_black_packet(uint32_t out[6],const uint32_t original[6],uint32_t word,uint32_t code,const uint32_t black[4]){
 if(!out||!original||!p50_black_profile(black,word))return 0;
 uint32_t packet[6];if(!p49_wb_make_packet(packet,original,word,code))return 0;
 /* The accepted two profiles reproduce original80390ee4 exactly. Never
  * extend this fourfold encoding to an unobserved black value by inference. */
 uint32_t h[4];for(unsigned i=0;i<4;i++)h[i]=black[i]*4u;
 if(p49_wb_selector(word)==1u){h[0]-=p49_wb_div65536(word>>21);h[1]-=256u;h[2]-=256u;h[3]-=p49_wb_div65536((word>>10)&2047u);}
 packet[0]=h[0]|(h[1]<<16);packet[1]=h[2]|(h[3]<<16);
 for(unsigned i=0;i<6;i++)out[i]=packet[i];return 1;
}
#endif
