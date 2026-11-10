/* SPDX-License-Identifier: AGPL-3.0-only.
 * Original NX500 clock-selector registers, read-only, infrequently while
 * recording. The same addresses are used by the existing kind-20 observer
 * and by the original firmware's clock readback. A failed read is evidence
 * of unavailability, never a reason to change clocks or stop recording.
 */
#ifndef NXL_VIDEO_CLOCK_CAPTURE_H
#define NXL_VIDEO_CLOCK_CAPTURE_H
#include "video_path_capture.h"
#define VP_CLOCK_OFFSET (VP_PATH_OFFSET+sizeof(struct vp_path_ring))
#define VP_CLOCK_CAPACITY 3u
#define VP_CLOCK_XOR 0x6039acb7u
static const uint32_t vp_clock_address[9]={0x10400038u,0x1040003cu,0x10400040u,0x10400044u,0x10400048u,0x1040004cu,0x10400050u,0x10400058u,0x10400028u};
struct vp_clock_record {uint32_t sequence,elapsed_ms,status,scene,clock[9],DDR_A_Hz,DDR_status,commit;};
struct vp_clock_ring {char magic[8];uint32_t schema,record_bytes,capacity,total,reserved[2];struct vp_clock_record record[VP_CLOCK_CAPACITY];};
struct vp_recording_watch {uint32_t source,forwarded;unsigned initialized,recorded;};
/* Called at most once per second. Only a changing original VideoSrc
 * forwarded counter denotes recording progress; pointer changes are reset. */
static int vp_recording_active(struct vp_recording_watch*w,const struct vp_record*s){
 int active=0;
 if((s->valid&(VP_SOURCE|VP_CHANGED))==VP_SOURCE){
  uint32_t source=s->source[0],forwarded=s->source[2];
  if(w->initialized&&source==w->source&&forwarded>w->forwarded)active=1;
  w->source=source;w->forwarded=forwarded;w->initialized=1;
 }else w->initialized=0;
 if(active)w->recorded=1;
 return active;
}
static void vp_clock_init(struct vp_clock_ring*p){memset(p,0,sizeof*p);memcpy(p->magic,"NXLCLK1",8);p->schema=3;p->record_bytes=sizeof(struct vp_clock_record);p->capacity=VP_CLOCK_CAPACITY;}
static void vp_clock_capture(const struct vp_reader*r,struct vp_clock_ring*p,uint32_t milliseconds,uint32_t ddr_hz,uint32_t ddr_status){
 struct vp_clock_record z;memset(&z,255,sizeof z);z.sequence=p->total;z.elapsed_ms=milliseconds;z.status=3;z.DDR_A_Hz=ddr_hz;z.DDR_status=ddr_status;
 uint32_t d1,d2,s1,s2,second[9];
 int ok=vp_rw(r,0x8146e438u,&d1)&&vp_rw(r,0x813f1b30u,&s1);
 for(unsigned i=0;i<9;i++)if(!vp_rw(r,vp_clock_address[i],z.clock+i))ok=0;
 for(unsigned i=0;i<9;i++)if(!vp_rw(r,vp_clock_address[i],second+i))ok=0;
 if(!vp_rw(r,0x8146e438u,&d2)||!vp_rw(r,0x813f1b30u,&s2))ok=0;
 if(ok){z.scene=s1;z.status=(d1==d2&&s1==s2&&!memcmp(z.clock,second,sizeof second))?1u:2u;}
 z.commit=vp_hash(&z,offsetof(struct vp_clock_record,commit))^VP_CLOCK_XOR;
 /* Optional first preview read is pinned; subsequent recording evidence
  * uses the other two slots. This retains before/after comparison at no
  * additional hot sampling rate or RAM allocation. */
 unsigned slot=p->reserved[0]&&p->total?1u+(p->total-1u)%2u:p->total%VP_CLOCK_CAPACITY;
 struct vp_clock_record*out=p->record+slot;
 out->commit=0;__sync_synchronize();memcpy(out,&z,offsetof(struct vp_clock_record,commit));__sync_synchronize();out->commit=z.commit;__sync_synchronize();p->total++;
}
_Static_assert(sizeof(struct vp_clock_record)==64,"bounded clock evidence");
_Static_assert(VP_CLOCK_OFFSET+sizeof(struct vp_clock_ring)<=VP_HEADER_BYTES,"clock evidence fits header padding");
#endif
