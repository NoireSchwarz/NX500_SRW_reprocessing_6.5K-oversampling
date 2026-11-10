/* SPDX-License-Identifier: AGPL-3.0-only. Read-only native clock/counter evidence. */
#ifndef NXL_VIDEO_RUNTIME_OBSERVATION_H
#define NXL_VIDEO_RUNTIME_OBSERVATION_H
#include "video_fullwidth_contract.h"
#define VFR_MAGIC 0x3152564eu
static const uint32_t vfr_clock[9]={0x10400038u,0x1040003cu,0x10400040u,0x10400044u,0x10400048u,0x1040004cu,0x10400050u,0x10400058u,0x10400028u};
/* Original CLiveviewDebugConsole::dump_reg reads these non-clearing RAM counters. */
static const uint32_t vfr_counter[4]={0x8154160cu,0x815415c8u,0x815415a4u,0x81541580u};
struct vfr_sample {uint32_t magic,schema,status,mask,driver_before,driver_after,scene_before,scene_after,counters[4],clock_before[9],clock_after[9],hash,commit;};
static int vfr_capture(vfw_reader read,void*c,struct vfr_sample*s){
 memset(s,0,sizeof*s);s->magic=VFR_MAGIC;s->schema=1;
 if(!read(c,VFW_DRIVER+8,&s->driver_before,4)||!read(c,0x813f1b30u,&s->scene_before,4))goto unavailable;s->mask|=1;
 for(unsigned i=0;i<9;i++)if(!read(c,vfr_clock[i],s->clock_before+i,4))goto unavailable;s->mask|=2;
 for(unsigned i=0;i<4;i++)if(!read(c,vfr_counter[i],s->counters+i,4))goto unavailable;s->mask|=4;
 for(unsigned i=0;i<9;i++)if(!read(c,vfr_clock[i],s->clock_after+i,4))goto unavailable;s->mask|=8;
 if(!read(c,VFW_DRIVER+8,&s->driver_after,4)||!read(c,0x813f1b30u,&s->scene_after,4))goto unavailable;
 s->status=s->driver_before==s->driver_after&&s->scene_before==s->scene_after&&!memcmp(s->clock_before,s->clock_after,sizeof s->clock_before)?1u:2u;goto finish;
unavailable:s->status=3;
finish:s->hash=vfw_hash(s,sizeof*s-8u);s->commit=VFR_MAGIC^s->status^s->hash;return s->status==1u;
}
_Static_assert(sizeof(struct vfr_sample)==128,"bounded native video counters and clock selectors");
#endif
