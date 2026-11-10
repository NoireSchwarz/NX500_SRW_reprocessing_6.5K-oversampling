/* SPDX-License-Identifier: AGPL-3.0-only. NX500 1.12 frontend control RAM.
 * No image pointers, MMIO, native calls or writes. Geometry/readout rate are
 * not inferred from a cached pp_video_type. Bracketed reads are not atomic.
 */
#ifndef NX_VIDEO_FRONT_OBSERVATION_H
#define NX_VIDEO_FRONT_OBSERVATION_H
#include <stdint.h>
#include <string.h>
#define VF_MAGIC 0x3146564eu
#define VF_ROOT 0x81541808u
#define VF_SECONDARY_ROOT 0x815417fcu
#define VF_VTABLE 0x80587498u
#define VF_WORDS (0x3a4u/4u)
struct vf_sample {
 uint32_t magic,schema,status,mask;
 uint32_t roots_before[2],roots_after[2],vtable_before,vtable_after;
 uint32_t object[VF_WORDS],checksum,commit;
};
typedef int(*vf_reader)(void*,uint32_t,void*,unsigned);
static uint32_t vf_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int vf_ram(uint32_t p){return !(p&3u)&&p>=0x81000000u&&(uint64_t)p+0x3a4u<=0x94000000ULL;}
static int vf_capture(vf_reader read,void*ctx,struct vf_sample*s){
 uint32_t again[VF_WORDS];memset(s,0,sizeof *s);s->magic=VF_MAGIC;s->schema=1;
 if(!read(ctx,VF_ROOT,s->roots_before,4)||!read(ctx,VF_SECONDARY_ROOT,s->roots_before+1,4)){s->status=3;goto finish;}
 if(!s->roots_before[0]){s->status=2;goto finish;}
 if(!vf_ram(s->roots_before[0])){s->status=5;goto finish;}
 if(s->roots_before[1]!=s->roots_before[0]+0x230u){s->status=4;goto finish;}
 if(!read(ctx,s->roots_before[0],&s->vtable_before,4)){s->status=3;goto finish;}
 if(s->vtable_before!=VF_VTABLE){s->status=2;goto finish;}
 if(!read(ctx,s->roots_before[0],s->object,sizeof s->object)){s->status=3;goto finish;}s->mask|=1;
 if(!read(ctx,s->roots_before[0],again,sizeof again)){s->status=3;goto finish;}s->mask|=2;
 if(!read(ctx,VF_ROOT,s->roots_after,4)||!read(ctx,VF_SECONDARY_ROOT,s->roots_after+1,4)||!read(ctx,s->roots_before[0],&s->vtable_after,4)){s->status=3;goto finish;}
 if(memcmp(s->roots_before,s->roots_after,8)||s->vtable_after!=VF_VTABLE||s->object[0]!=VF_VTABLE||memcmp(s->object,again,sizeof again)){s->status=4;goto finish;}
 s->status=1;
finish:
 s->checksum=vf_hash(s,sizeof *s-8u);s->commit=VF_MAGIC^s->status^s->checksum;return s->status==1;
}
static int vf_valid(const struct vf_sample*s){return s->magic==VF_MAGIC&&s->schema==1&&s->status>=1&&s->status<=5&&s->mask<=3&&s->checksum==vf_hash(s,sizeof *s-8u)&&s->commit==(VF_MAGIC^s->status^s->checksum);}
_Static_assert(sizeof(struct vf_sample)==980,"fixed frontend observation ABI");
#endif
