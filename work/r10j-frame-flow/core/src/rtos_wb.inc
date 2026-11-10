/* SPDX-License-Identifier: AGPL-3.0-only. P49: full C0 WB packet, mode0/CS1.
 * Native dynamic source-delta calibration is not reproduced; audit states0.
 * Every frame uses the same
 * original index1 setter with an immutable first-config template. SRA is
 * observation-only. Immediate restore visibility is reported, not guessed. */
#include "wb_contract.h"
static volatile uint32_t *wb(void){return ptr(ctx.cap+P41_OFFSET);}
static __attribute__((noinline,section(".text.wb"))) void wb_snapshot(unsigned dest){volatile uint32_t *w=wb();for(unsigned b=0;b<2;b++)for(unsigned i=0;i<6;i++)w[dest+b*6+i]=*ptr((b?P41_SRA:P41_LCAC)+4*i);barrier();}
static __attribute__((noinline,section(".text.wb"))) int wb_primary_matches(unsigned dest,unsigned expected){volatile uint32_t *w=wb();for(unsigned i=0;i<6;i++)if(w[dest+i]!=w[expected+i])return 0;return 1;}
static __attribute__((noinline,section(".text.wb"))) int wb_target_matches(unsigned dest){volatile uint32_t *w=wb();for(unsigned i=0;i<6;i++)if(w[dest+i]!=w[P49_WB_TARGET_WORD+i])return 0;return 1;}
static __attribute__((noinline,section(".text.wb"))) int wb_secondary_stable(void){volatile uint32_t *w=wb();const unsigned slots[]={96,112,128,228};for(unsigned k=0;k<4;k++)for(unsigned i=6;i<12;i++)if(w[slots[k]+i]!=w[80+i])return 0;return 1;}
static __attribute__((noinline,section(".text.wb"))) int wb_restore(void){
 volatile uint32_t *w=wb();if(!w[65])return 1;
 if(w[221])return w[69]==0;
 if(w[71]){w[72]=1;barrier();return 0;}
 w[221]=1;barrier();w[69]=(uint32_t)native(0x8017d33cu,1,(uint32_t)(w+80),0);
 wb_snapshot(144);w[66]=wb_primary_matches(144,80)?1u:0u;
 if(!w[66]&&!w[69])w[73]=134;barrier();return !w[69];
}
static __attribute__((noinline,section(".text.wb"))) int wb_prepare(void){
 volatile uint32_t *w=wb();if(!p41_request_valid((const uint32_t*)w,ctx.nonce)||*ptr(0x813ebf5cu)!=0x20810000u)return 0;
 w[74]=P49_WB_AUDIT_SIGNATURE;wb_snapshot(240);w[252]=1;barrier();return 1;
}
static __attribute__((noinline,section(".text.wb"))) int wb_apply(unsigned source_wb,const volatile uint32_t *fixed12){
 volatile uint32_t *w=wb();if(!fixed12||source_wb>1||w[49]!=source_wb||!w[252]||w[220]||!p41_request_valid((const uint32_t*)w,ctx.nonce)||*ptr(0x813ebf5cu)!=0x20810000u)return 0;
 w[64]=1;w[219]=source_wb;w[222]=1;
 for(unsigned i=0;i<12;i++)w[80+i]=fixed12[i];
 uint32_t target[6],known_template[6];for(unsigned i=0;i<6;i++)known_template[i]=w[80+i];
 if(source_wb){uint32_t black[4];for(unsigned i=0;i<4;i++)black[i]=w[16+i];if(!p50_wb_black_packet(target,known_template,w[51],w[52],black))return 0;}
 else for(unsigned i=0;i<6;i++)target[i]=known_template[i];
 for(unsigned i=160;i<219;i++)w[i]=0;
 for(unsigned i=0;i<6;i++)w[P49_WB_TARGET_WORD+i]=target[i];
 w[166]=p41_checksum(target,6);w[167]=P49_WB_PROFILE;w[168]=w[57];w[169]=w[58];
 w[170]=w[59];w[171]=w[60];w[172]=0;w[173]=w[51];w[174]=w[52];w[175]=source_wb;
 w[176]=P50_BLACK_AUDIT_SIGNATURE;w[177]=P50_BLACK_AUDIT_SCHEMA;w[182]=w[62];w[187]=source_wb;
 for(unsigned i=0;i<4;i++){w[178+i]=w[16+i];w[183+i]=(target[i/2]>>((i&1u)*16u))&65535u;}
 if((*ptr(0x817900e0u)&1u)&&*ptr(0x817900e8u)==0x80627b28u){for(unsigned i=0;i<3;i++)w[224+i]=ptr(0x817900e8u+2488)[i];w[227]=*ptr(0x817900e8u+2316);}
 w[65]=1;w[220]=1;barrier();w[67]=(uint32_t)native(0x8017d33cu,1,(uint32_t)(w+P49_WB_TARGET_WORD),0);
 wb_snapshot(96);w[75]=wb_target_matches(96);w[64]=2;barrier();return w[67]==0;
}
