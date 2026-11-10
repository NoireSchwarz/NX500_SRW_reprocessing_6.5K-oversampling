/* SPDX-License-Identifier: AGPL-3.0-only. Fixed RO evidence, no native calls
 * or writes. Clock brackets qualify LDC reads; a sample is never a DMA or
 * resource-retirement proof. Debug offsets come from unchanged native
 * _udd_ep_ldc_print_debug_info(0x33f14), not a scan of unknown registers. */
#ifndef NXL_LDC_OBSERVATION_H
#define NXL_LDC_OBSERVATION_H
#include <stdint.h>
#include <string.h>
static const uint32_t lh_top_offsets[]={0x1000u,0x1008u,0x100cu,0x1020u,0x1024u,0x1030u,0x1034u,0x1038u,0x1048u,0x1070u,0x1074u,0x1078u,0x107cu,0x1080u,0x10c8u,0x10ccu,0x10d0u,0x10d4u,0x10d8u,0x1100u,0x1104u,0x110cu,0x1110u,0x1114u,0x1118u,0x1200u,0x1204u,0x120cu,0x1210u,0x1214u,0x1218u,0x1300u,0x1304u,0x1308u,0x130cu,0x1310u,0x1318u,0x131cu,0x1320u,0x1324u,0x1328u,0x132cu,0x1400u,0x1404u,0x1408u,0x140cu,0x1410u,0x1418u,0x141cu,0x1420u,0x1424u,0x1428u,0x142cu,0x1700u,0x1704u,0x1708u,0x170cu,0x1710u,0x1718u,0x171cu,0x1720u,0x1724u,0x172cu,0x1800u,0x1804u,0x1808u,0x180cu,0x1810u,0x1818u,0x181cu,0x1820u,0x1824u,0x182cu};
#define LH_TOP_COUNT (sizeof lh_top_offsets/sizeof *lh_top_offsets)
static const uint32_t lh_config_offsets[]={0x0u,0x4u,0x8u,0xcu,0x10u,0x14u,0x18u,0x1cu,0x20u,0x24u,0x28u,0x2cu,0x30u,0x34u,0x38u,0x3cu,0x40u,0x44u,0x48u,0x4cu,0x50u,0x54u,0x74u,0x78u,0x7cu,0x80u,0x84u,0x88u,0x8cu,0x90u,0x94u,0x98u,0x9cu,0xa0u,0xa4u,0xa8u,0xacu,0xb0u,0xb4u,0xb8u,0xbcu,0xc0u,0xc4u,0xc8u,0xccu,0xd0u,0xd4u,0xf4u,0xf8u,0xfcu,0x100u,0x104u,0x180u,0x184u,0x188u,0x190u,0x194u,0x198u,0x1a0u,0x1a4u,0x1a8u,0x1acu,0x1b0u,0x1b4u,0x1b8u,0x1bcu,0x1c0u,0x1c4u,0x1c8u,0x1ccu,0x1d0u,0x1d4u,0x1d8u,0x1dcu};
#define LH_CONFIG_COUNT (sizeof lh_config_offsets/sizeof *lh_config_offsets)
static const uint32_t lh_debug_offsets[]={0x408u,0x40cu,0x410u,0x414u,0x418u,0x41cu,0x420u,0x424u,0x428u,0x42cu,0x430u,0x434u,0x438u,0x43cu,0x440u,0x444u,0x448u,0x44cu,0x450u,0x454u};
#define LH_DEBUG_COUNT (sizeof lh_debug_offsets/sizeof *lh_debug_offsets)
enum {LH_TOP_OK=1,LH_CONFIG_OK=2,LH_DEBUG_OK=4,LH_CLOCK_STABLE=8,LH_GATED=16,LH_READ_FAILED=32,LH_CLOCK_CHANGED=64,LH_DEBUG_OUTSIDE_MAPPING=128};
struct lh_snapshot {uint32_t schema,bytes,phase,flags,clock_before[2],clock_after[2],top[LH_TOP_COUNT],config[LH_CONFIG_COUNT],debug[LH_DEBUG_COUNT];};
_Static_assert(sizeof(struct lh_snapshot)==700,"native LDC observation frame ABI");
typedef int(*lh_read_fn)(void*,unsigned,uint32_t,void*,unsigned);
static int lh_known_span(unsigned bank,uint32_t off,unsigned bytes){
 if(bank>1||!bytes||(bytes&3u)||(off&3u)||bytes>512)return 0;
 const uint32_t*sets[2]={bank?lh_config_offsets:lh_top_offsets,lh_debug_offsets};
 unsigned counts[2]={bank?LH_CONFIG_COUNT:LH_TOP_COUNT,LH_DEBUG_COUNT};
 for(unsigned k=0;k<(bank?2u:1u);k++)for(unsigned i=0;i<counts[k];i++)if(sets[k][i]==off){
  unsigned words=bytes/4;if(i+words>counts[k])continue;unsigned j=1;for(;j<words;j++)if(sets[k][i+j]!=off+4*j)break;if(j==words)return 1;
 }return 0;
}
static int lh_read_list(lh_read_fn rd,void*ctx,unsigned bank,const uint32_t*offsets,unsigned count,uint32_t*out){
 int ok=1;for(unsigned i=0;i<count;){unsigned n=1;while(i+n<count&&offsets[i+n]==offsets[i]+n*4)n++;
  if(!rd(ctx,bank,offsets[i],out+i,n*4))ok=0;i+=n;
 }return ok;
}
static void lh_capture(lh_read_fn rd,void*ctx,unsigned ldc_bytes,unsigned phase,struct lh_snapshot*s){
 memset(s,0,sizeof *s);s->schema=1;s->bytes=sizeof *s;s->phase=phase;
 if(!rd(ctx,0,0x1020,s->clock_before,8)){s->flags=LH_READ_FAILED;return;}
 if(lh_read_list(rd,ctx,0,lh_top_offsets,LH_TOP_COUNT,s->top))s->flags|=LH_TOP_OK;else s->flags|=LH_READ_FAILED;
 if((s->clock_before[0]|s->clock_before[1])&8u)s->flags|=LH_GATED;
 else{
  if(ldc_bytes>=0x1e0u&&lh_read_list(rd,ctx,1,lh_config_offsets,LH_CONFIG_COUNT,s->config))s->flags|=LH_CONFIG_OK;else s->flags|=LH_READ_FAILED;
  if(ldc_bytes<0x458u)s->flags|=LH_DEBUG_OUTSIDE_MAPPING;
  else if(lh_read_list(rd,ctx,1,lh_debug_offsets,LH_DEBUG_COUNT,s->debug))s->flags|=LH_DEBUG_OK;else s->flags|=LH_READ_FAILED;
 }
 if(!rd(ctx,0,0x1020,s->clock_after,8)){s->flags=(s->flags&~(LH_CONFIG_OK|LH_DEBUG_OK))|LH_READ_FAILED;return;}
 if(!memcmp(s->clock_before,s->clock_after,8))s->flags|=LH_CLOCK_STABLE;
 else s->flags=(s->flags&~(LH_CONFIG_OK|LH_DEBUG_OK))|LH_CLOCK_CHANGED;
}
#endif
