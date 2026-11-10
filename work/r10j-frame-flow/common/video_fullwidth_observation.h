/* SPDX-License-Identifier: AGPL-3.0-only. Typed normal RAM, no image data. */
#ifndef NXL_VIDEO_FULLWIDTH_OBSERVATION_H
#define NXL_VIDEO_FULLWIDTH_OBSERVATION_H
#include "video_fullwidth_contract.h"
#define VFO_MAGIC 0x314f574eu
struct vfo_sample {uint32_t magic,schema,status,mask;struct vfw_snapshot snapshot;uint32_t hash,commit;};
static int vfo_capture(vfw_reader read,void*c,struct vfo_sample*s){memset(s,0,sizeof*s);s->magic=VFO_MAGIC;s->schema=3;s->status=vfw_capture(read,c,&s->snapshot)?1u:4u;s->mask=s->status==1?3u:0u;s->hash=vfw_hash(s,sizeof*s-8u);s->commit=VFO_MAGIC^s->status^s->hash;return s->status==1;}
_Static_assert(sizeof(struct vfo_sample)==696,"typed PP6/19, movie buffers, power, scaler builder and native preset observation");
#endif
