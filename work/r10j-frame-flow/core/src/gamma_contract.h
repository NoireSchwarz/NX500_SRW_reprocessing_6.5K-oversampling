/* SPDX-License-Identifier: AGPL-3.0-only. Owned, nonce-bound Still Gamma test.
 * 0=factory, 1=source C0 19 nodes, 2=source with bounded midtone lift.
 * No continuous-stream update, extra SRAM reload, or new frame pass. */
#ifndef NXL_GAMMA_TEST_CONTRACT_H
#define NXL_GAMMA_TEST_CONTRACT_H
#include <stdint.h>
#include "front_layout.h"
#include "batch_contract.h"
#define GT_OFFSET 0x03e20000u
#define GT_BYTES 0x4000u
#define GT_AUDIT_BYTES 2048u
#define GT_MAGIC 0x314d474eu
#define GT_DDR 0xbb71f228u
#define GT_DDR0 0xbb71d228u
#define GT_WORDS 108u
#define GT_SAVED 128u
#define GT_TARGET 236u
#define GT_CALCULATOR 0x8038f90cu
#define GT_WRITER 0x8049e4d4u
enum {GT_READY=64,GT_CALLED,GT_STATUS,GT_TARGET_MATCH,GT_RESTORE_CALLED,
 GT_RESTORE_STATUS,GT_RESTORED,GT_ERROR,GT_CONFIG_LOADS,GT_AFTER_START,
 GT_AFTER_COMPLETE,GT_BANK0_HASH,GT_BANK0_SAME,GT_BEFORE_LOAD,GT_CONFIG_RETURN,GT_RESTORE_BANK0_SAME,
 GT_INITIAL_LOAD_STATUS,GT_REFRESH_CALLED,GT_REFRESH_RETURN,GT_FINAL_LOAD_CALLED,
 GT_FINAL_LOAD_STATUS,GT_MANAGER_STYLE,GT_NATIVE_GROUP,GT_NATIVE_GROUP_VTABLE};
static inline int gt_request_valid(const uint32_t *q,uint32_t nonce,uint32_t cc_sum){
 if(q[0]!=GT_MAGIC||q[1]!=1||q[2]!=nonce||!nonce||q[3]>2||q[4]!=cc_sum||q[5]!=1||q[6]!=1||q[63]!=p48_batch_crc(q,252))return 0;
 for(unsigned i=8;i<16;i++)if(q[i])return 0;
 for(unsigned i=54;i<63;i++)if(q[i])return 0;
 if(q[16]||q[34]!=16383u||(int32_t)q[35]>0||q[53]!=1023u)return 0;
 for(unsigned i=0;i<19;i++)if(q[16+i]>16383u||(int32_t)q[35+i]<-1024||(int32_t)q[35+i]>1023||(i&&(q[16+i]<=q[15+i]||(int32_t)q[35+i]<(int32_t)q[34+i])))return 0;
 return 1;
}
static inline uint32_t gt_y(uint32_t mode,uint32_t y){return mode==2&&(int32_t)y>0?y+y*(1023u-y)/4092u:y;}
static inline int gt_audit_valid(const uint32_t*q,uint32_t nonce,uint32_t cc_sum){
 if(!gt_request_valid(q,nonce,cc_sum)||q[GT_ERROR]||q[GT_READY]!=1||q[GT_CONFIG_LOADS]!=2||q[GT_BEFORE_LOAD]!=1||q[GT_CONFIG_RETURN]!=1||q[GT_BANK0_SAME]!=1||q[GT_INITIAL_LOAD_STATUS]||q[GT_REFRESH_CALLED]!=1||q[GT_REFRESH_RETURN]!=1||q[GT_FINAL_LOAD_CALLED]!=1||q[GT_FINAL_LOAD_STATUS]||q[GT_MANAGER_STYLE])return 0;
 if(!q[3])return !q[GT_CALLED]&&!q[GT_RESTORE_CALLED];
 /* In-flight DDR samples are observations. A changed sample must never
  * cause a second write or hold a workspace whose DMA is already retired. */
 return q[GT_CALLED]==1&&q[GT_STATUS]==1&&q[GT_TARGET_MATCH]==1&&q[GT_RESTORE_CALLED]==1&&!q[GT_RESTORE_STATUS]&&q[GT_RESTORED]==1&&q[GT_AFTER_START]<=1&&q[GT_AFTER_COMPLETE]<=1;
}
_Static_assert(GT_OFFSET>=0x03e00000u+0x20000u,"Gamma after ISP audit");
_Static_assert(GT_OFFSET+GT_BYTES<P39_TARGET_OFFSET-16384u,"Gamma before output guard");
_Static_assert(GT_OFFSET>P39_SOURCE_OFFSET+P39_SOURCE_BYTES+16384u,"Gamma outside Bayer input");
_Static_assert((GT_TARGET+GT_WORDS)*4u<=GT_AUDIT_BYTES,"Gamma bounded record");
#endif
