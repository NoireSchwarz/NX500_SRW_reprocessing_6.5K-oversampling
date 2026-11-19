/* SPDX-License-Identifier: AGPL-3.0-only. Independent 100ms frame-ring drain.
 * Heavy snapshots may back off; this deadline never backs off silently.
 */
#ifndef NXL_VIDEO_FAST_CAPTURE_H
#define NXL_VIDEO_FAST_CAPTURE_H
#include "video_counter_capture.h"
#include "video_latency_capture.h"
#define VF_META_OFFSET 15424u
#define VF_PERIOD_MS 100u
#define VF_CAPACITY 12000u
#define VF_DATA_OFFSET (VP_HEADER_BYTES+VP_MAX_SAMPLES*sizeof(struct vp_record))
#define VF_XOR 0xa7316d29u
struct vf_header {
 char magic[8];uint32_t schema,record_bytes,capacity,count,period_ms,flags;
 uint64_t last_ns,max_gap_ns,lost_sequences,cpu_ns;
 uint32_t hash,reserved[15];
};
struct vf_record {
 uint64_t begin_ns,end_ns;uint32_t sequence,driver,preset,valid;
 uint32_t latency[64],ep_observer[4],commit,reserved;
};
#define VF_END (VF_DATA_OFFSET+VF_CAPACITY*sizeof(struct vf_record))
static void vf_init(struct vf_header*h){memset(h,0,sizeof*h);memcpy(h->magic,"NXLFAST1",8);h->schema=4;h->record_bytes=sizeof(struct vf_record);h->capacity=VF_CAPACITY;h->period_ms=VF_PERIOD_MS;}
static int vf_capture_attempt(const struct vp_reader*r,struct vf_header*h,struct vf_record*records,uint64_t(*now)(void*),void*c){
 if(h->count>=VF_CAPACITY){h->flags|=4u;return 0;}
 struct vf_record z;uint32_t header[8]={0},driver,preset;memset(&z,0,sizeof z);
 z.begin_ns=now(c);z.sequence=h->count;
 int ok=vp_rw(r,0x8146e438u,&z.driver)&&vp_rw(r,0x813ebf60u,&z.preset)&&
  r->physical(r->context,VP_LATENCY_DATA,z.latency,sizeof z.latency)&&
  r->physical(r->context,VP_LATENCY_DATA,header,sizeof header);
 unsigned schema=vp_timer_schema(header),ring=(schema==5u||schema==6u)?7u:(schema==10u||schema==11u)?4u:schema?8u:0u;
 if(ok&&ring&&!memcmp(header,z.latency,header[1]==5u?28u:32u))z.valid|=1u;
 /* Missing/unarmed producer is decided after the bounded retry. */
 if(vp_rw(r,0x8146e438u,&driver)&&vp_rw(r,0x813ebf60u,&preset)&&driver==z.driver&&preset==z.preset)z.valid|=2u;
 /* This prefix is owned only by timer6. Two reads reject a changing counter. */
 if((z.valid&1u)&&header[1]==6u){uint32_t ep[4];
  if(r->physical(r->context,0x817dbbc8u,z.ep_observer,sizeof z.ep_observer)&&
     r->physical(r->context,0x817dbbc8u,ep,sizeof ep)&&!memcmp(ep,z.ep_observer,sizeof ep)&&!ep[3])z.valid|=4u;
  else memset(z.ep_observer,0,sizeof z.ep_observer);
 }
 /* Version3: raw native completion flags; four masks retain failed reads.
  * Full16-word bracketing is separate from legacy ring-read availability. */
 if((z.valid&1u)&&header[1]==11u){
  static const uint32_t a[4]={0x2081040cu,0x20810418u,0x208210c8u,0x208210ccu};
  uint32_t after[16];memset(z.ep_observer,255,sizeof z.ep_observer);z.valid|=8u;
  for(unsigned j=0;j<4;j++)if(vp_rw(r,a[j],z.ep_observer+j))z.valid|=1u<<(4u+j);
  if(r->physical(r->context,VP_LATENCY_DATA,after,64u)&&!memcmp(after,z.latency,64u))z.valid|=256u;
 }
 z.end_ns=now(c);
 z.commit=vp_hash(&z,offsetof(struct vf_record,commit))^VF_XOR;
 records[h->count]=z;__sync_synchronize();h->count++;return 1;
}
static uint32_t vf_header_hash(const struct vf_header*h){
 struct vf_header z=*h;z.hash=0;return vp_hash(&z,sizeof z);
}
static int vf_capture(const struct vp_reader*r,struct vf_header*h,struct vf_record*records,uint64_t(*now)(void*),void*c){
 unsigned first=h->count;if(!vf_capture_attempt(r,h,records,now,c))return 0;
 struct vf_record*z=records+first;
 if((z->valid&3u)!=3u&&vp_timer_common_header(z->latency)&&h->count<VF_CAPACITY&&now(c)-z->begin_ns<2000000u){
  z->valid|=512u;z->commit=vp_hash(z,offsetof(struct vf_record,commit))^VF_XOR;h->reserved[1]++;
  if(!vf_capture_attempt(r,h,records,now,c))return 0;
  z=records+h->count-1;z->valid|=1024u;z->commit=vp_hash(z,offsetof(struct vf_record,commit))^VF_XOR;
  if((z->valid&3u)==3u)h->reserved[2]++;
 }
 h->reserved[0]++;
 if((z->valid&3u)!=3u){h->flags|=8u;h->reserved[3]++;}
 if(h->last_ns&&z->begin_ns>=h->last_ns){uint64_t gap=z->begin_ns-h->last_ns;if(gap>h->max_gap_ns)h->max_gap_ns=gap;if(gap>200000000u)h->flags|=2u;}
 h->last_ns=z->begin_ns;
 if(z->valid&1u){
  unsigned schema=vp_timer_schema(z->latency),ring=(schema==5u||schema==6u)?7u:(schema==10u||schema==11u)?4u:schema?8u:0u;
  for(unsigned i=first;i>0;){struct vf_record*old=records+--i;
   if(old->valid&512u)continue;
   if((old->valid&3u)!=3u)break;
   if(old->latency[0]==z->latency[0]){uint32_t a=old->latency[2],b=z->latency[2],delta=b-a;if(b<a&&a&&b)delta--;if(delta<0x80000000u&&delta>ring)h->lost_sequences+=delta-ring;}
   break;
  }
 }
 return 1;
}

_Static_assert(sizeof(struct vf_header)==128,"fast header");
_Static_assert(sizeof(struct vf_record)==312,"fast read with monotonic bounds");
_Static_assert(VF_META_OFFSET+sizeof(struct vf_header)<=VP_HEADER_BYTES,"outside pinned phase evidence");
#endif
