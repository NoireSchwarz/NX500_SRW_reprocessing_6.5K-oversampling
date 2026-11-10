/* SPDX-License-Identifier: AGPL-3.0-only.
 * P49 mode0/CS1 WB candidate, derived from original80390ee4 and the actual
 * CIpcWBD5 constructor widths[11,11,2,1]. Dynamic source-delta calibration is
 * explicitly NOT reproduced. Unknown gain-word bits18/19/31 are preserved.
 */
#ifndef P49_WB_NATIVE_PACKET_H
#define P49_WB_NATIVE_PACKET_H
#include <stdint.h>
#define P49_WB_GAIN_MASK 0x7ff00000u
#define P49_WB_OFFSET_MASK 0x0003ffffu
#define P49_WB_UNASSIGNED_MASK 0x800c0000u
#define P49_WB_PROFILE 1u
#define P49_WB_C0_MAGIC 0x433f2149u
#define P49_WB_C0_REVISION 20150126u
#define P49_WB_C0_BYTES 2048u
static inline unsigned p49_wb_selector(uint32_t word){return (word>>8)&3u;}
static inline unsigned p49_wb_flag(uint32_t word){return (word>>7)&1u;}
static inline unsigned p49_wb_code_selector(uint32_t code){return code<=176u?0u:code<=208u?1u:2u;}
/* Exact floor(65536/d) for validated64..2047 without a freestanding ARM
 * runtime divide dependency. Maximum intermediate product is below2^22. */
static inline uint32_t p49_wb_div65536(uint32_t d){uint32_t q=0;
 for(unsigned bit=11;bit;bit--){uint32_t next=q|(1u<<(bit-1));if(next*d<=65536u)q=next;}
 return q;
}
static inline int p49_wb_word_valid(uint32_t word,uint32_t code,const uint32_t gains[4]){
 if(!gains||code>255u||(word&127u)!=127u||p49_wb_selector(word)>2u||
    p49_wb_selector(word)!=p49_wb_code_selector(code)||p49_wb_flag(word)!=(code>208u))return 0;
 for(unsigned i=0;i<4;i++)if(gains[i]<64u||gains[i]>2047u)return 0;
 return gains[1]==256u&&gains[2]==256u&&gains[0]==(word>>21)&&gains[3]==((word>>10)&2047u);
}
static inline int p49_wb_make_packet(uint32_t out[6],const uint32_t known_template[6],uint32_t word,uint32_t code){
 uint32_t gain[4]={word>>21,256u,256u,(word>>10)&2047u};
 if(!out||!known_template||!p49_wb_word_valid(word,code,gain))return 0;
 unsigned selector=p49_wb_selector(word);uint32_t h[4]={512u,512u,512u,512u};
 if(selector){uint32_t top=selector==1u?2048u:4080u,fixed=selector==1u?1792u:3840u;
  h[0]=top-p49_wb_div65536(gain[0]);h[1]=fixed;h[2]=fixed;h[3]=top-p49_wb_div65536(gain[3]);}
 out[0]=h[0]|(h[1]<<16);out[1]=h[2]|(h[3]<<16);
 for(unsigned i=0;i<4;i++)out[2+i]=(known_template[2+i]&P49_WB_UNASSIGNED_MASK)|(gain[i]<<20)|0x3e000u;
 return 1;
}
#endif
