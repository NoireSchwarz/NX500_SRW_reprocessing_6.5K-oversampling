/* SPDX-License-Identifier: AGPL-3.0-only. Pin one steady recording DMA/DDR
 * snapshot for each comparison generation. This is a bounded append-only
 * extension, so B evidence is not overwritten by the final A recording. */
#ifndef NXL_VIDEO_PHASE_CAPTURE_H
#define NXL_VIDEO_PHASE_CAPTURE_H
#define VP_PHASE_OFFSET 4096u
#define VP_PHASE_CAPACITY 16u
#define VP_PHASE_XOR 0x39a57b62u
struct vp_phase_record {uint32_t generation,phase,mark,commit;struct vp_bus_record bus;struct vp_clock_record clock;};
struct vp_phase_ring {char magic[8];uint32_t schema,record_bytes,capacity,count,overflow,reserved;struct vp_phase_record record[VP_PHASE_CAPACITY];};
static void vp_phase_init(struct vp_phase_ring*p){memset(p,0,sizeof*p);memcpy(p->magic,"NXLHWP1",8);p->schema=1;p->record_bytes=sizeof(struct vp_phase_record);p->capacity=VP_PHASE_CAPACITY;}
static int vp_phase_needed(struct vp_phase_ring*p,unsigned generation){
 for(unsigned i=0;i<p->count;i++)if(p->record[i].generation==generation)return 0;
 if(p->count>=VP_PHASE_CAPACITY){p->overflow=1;return 0;}
 return 1;
}
static void vp_phase_capture(const struct vp_reader*r,struct vp_phase_ring*p,const struct vp_record*s,unsigned elapsed,unsigned generation,unsigned phase,unsigned mark,uint32_t ddr[2]){
 if(!vp_phase_needed(p,generation))return;
 if(p->count>=VP_PHASE_CAPACITY){p->overflow=1;return;}
 struct vp_phase_record z;memset(&z,0,sizeof z);struct vp_bus_ring bus;struct vp_clock_ring clock;
 vp_bus_init(&bus);vp_clock_init(&clock);vp_bus_capture(r,&bus,s,elapsed,phase);vp_clock_capture(r,&clock,elapsed,ddr[0],ddr[1]);
 z.generation=generation;z.phase=phase;z.mark=mark;z.bus=bus.record[phase];z.clock=clock.record[0];
 /* Commit excludes its own word, but includes BOTH nested payloads. */
 z.commit=vp_hash(&z,sizeof z)^VP_PHASE_XOR;
 struct vp_phase_record*out=p->record+p->count;memcpy(out,&z,sizeof z);__sync_synchronize();p->count++;
}
_Static_assert(sizeof(struct vp_phase_record)==704,"fixed per-phase evidence");
_Static_assert(VP_PHASE_OFFSET+sizeof(struct vp_phase_ring)<=VP_HEADER_BYTES,"phase evidence fits Linux-only extension");
#endif
