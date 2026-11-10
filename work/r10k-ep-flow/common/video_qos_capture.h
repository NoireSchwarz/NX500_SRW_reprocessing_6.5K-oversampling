/* SPDX-License-Identifier: AGPL-3.0-only. Native read-only bus/EP snapshots.
 * Fits existing header padding. Only collect while UHD/E005 is running.
 * The HEVC bridge is read only after successful native encoder output. */
#ifndef NXL_VIDEO_QOS_CAPTURE_H
#define NXL_VIDEO_QOS_CAPTURE_H
#define VP_QOS_OFFSET 640u
#define VP_QOS_XOR 0x4c31ad87u
#define VP_QOS_REGS 17u
static const uint32_t vp_qos_address[VP_QOS_REGS]={
 0x1ff04d0cu,0x1ff04d10u,0x20820150u,0x208201a0u,0x20821110u,0x20821210u,0x2012f138u,0x20810308u,
 0x10400038u,0x1040003cu,0x10400040u,0x10400044u,0x10400048u,0x1040004cu,0x10400050u,0x10400058u,0x10400028u};
struct vp_qos_record {uint32_t sequence,elapsed_ms,status,phase,driver,scene,preset,IRQ_handler,valid_mask,value[VP_QOS_REGS],commit;};
struct vp_qos_ring {char magic[8];uint32_t schema,record_bytes,capacity,total,phase_mask,reserved;struct vp_qos_record record[3];};
static void vp_qos_init(struct vp_qos_ring*p){memset(p,0,sizeof*p);memcpy(p->magic,"NXLQOS1",8);p->schema=1;p->record_bytes=sizeof(struct vp_qos_record);p->capacity=3;}
static void vp_qos_capture(const struct vp_reader*r,struct vp_qos_ring*p,const struct vp_record*s,uint32_t milliseconds,unsigned phase){
 if(phase>2u)return;
 struct vp_qos_record z;memset(&z,255,sizeof z);z.sequence=p->total;z.elapsed_ms=milliseconds;z.status=1;z.phase=phase;z.valid_mask=0;z.driver=s->rtos[0];z.scene=s->rtos[1];z.preset=s->rtos[2];
 int bridge=phase==1u&&(s->valid&VP_HEVC)&&s->hevc[1]&&s->hevc[1]!=VP_UNKNOWN;uint32_t driver2,scene2,handler2;
 if(!vp_rw(r,0x813f09bcu,&z.IRQ_handler))z.status=3;
 for(unsigned i=0;i<VP_QOS_REGS;i++){
  if(i==6u&&!bridge)continue;
  uint32_t again;
  if(!vp_rw(r,vp_qos_address[i],z.value+i)||!vp_rw(r,vp_qos_address[i],&again)){z.status=3;continue;}
  z.valid_mask|=1u<<i;if(z.value[i]!=again&&z.status!=3u)z.status=2;
 }
 if(!vp_rw(r,0x8146e438u,&driver2)||!vp_rw(r,0x813f1b30u,&scene2)||!vp_rw(r,0x813f09bcu,&handler2))z.status=3;
 else if((driver2!=z.driver||scene2!=z.scene||handler2!=z.IRQ_handler)&&z.status!=3u)z.status=2;
 z.commit=vp_hash(&z,offsetof(struct vp_qos_record,commit))^VP_QOS_XOR;
 struct vp_qos_record*out=p->record+phase;out->commit=0;__sync_synchronize();memcpy(out,&z,offsetof(struct vp_qos_record,commit));__sync_synchronize();out->commit=z.commit;__sync_synchronize();if(phase==1u)p->phase_mask&=~4u;p->phase_mask|=1u<<phase;p->total++;
}
_Static_assert(sizeof(struct vp_qos_record)==108,"bounded QoS snapshot");
_Static_assert(VP_QOS_OFFSET>=sizeof(struct vp_header)&&VP_QOS_OFFSET+sizeof(struct vp_qos_ring)<=VP_PATH_OFFSET,"QoS uses only existing header padding");
#endif
