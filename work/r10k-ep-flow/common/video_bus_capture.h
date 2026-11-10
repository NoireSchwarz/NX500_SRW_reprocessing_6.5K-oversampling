/* SPDX-License-Identifier: AGPL-3.0-only. R10g DMA configuration snapshots.
 * Only original IPC/EP configuration addresses, never IRQ ACK/status windows.
 * The fixed extension replaces eight path slots; no buffer allocation grows. */
#ifndef NXL_VIDEO_BUS_CAPTURE_H
#define NXL_VIDEO_BUS_CAPTURE_H
#define VP_BUS_OFFSET (VP_CLOCK_OFFSET+sizeof(struct vp_clock_ring))
#define VP_BUS_REGS 136u
#define VP_BUS_MASKS 5u
#define VP_BUS_XOR 0x6d28a791u
static const uint32_t vp_bus_address[VP_BUS_REGS]={
0x20810908u,0x2081090cu,0x20810910u,0x20810914u,0x2081091cu,0x20810924u,0x20810980u,0x20810984u,0x20810988u,0x2081098cu,0x20810994u,0x2081099cu,0x20810a08u,0x20810a0cu,0x20810a10u,0x20810a14u,0x20810a1cu,0x20810a24u,0x20810a80u,0x20810a84u,0x20810a88u,0x20810a8cu,0x20810a94u,0x20810a9cu,0x20810b08u,0x20810b0cu,0x20810b10u,0x20810b14u,0x20810b1cu,0x20810b24u,0x20810b80u,0x20810b84u,0x20810b88u,0x20810b8cu,0x20810b94u,0x20810b9cu,0x20810c08u,0x20810c0cu,0x20810c10u,0x20810c14u,0x20810c1cu,0x20810c24u,0x20810c80u,0x20810c84u,0x20810c88u,0x20810c8cu,0x20810c94u,0x20810c9cu,0x20821300u,0x20821304u,0x20821308u,0x2082130cu,0x20821310u,0x20821400u,0x20821404u,0x20821408u,0x2082140cu,0x20821410u,0x20821500u,0x20821504u,0x20821508u,0x2082150cu,0x20821510u,0x20821600u,0x20821604u,0x20821608u,0x2082160cu,0x20821610u,0x20821380u,0x20821384u,0x20821388u,0x2082138cu,0x20821390u,0x20821480u,0x20821484u,0x20821488u,0x2082148cu,0x20821490u,0x20821580u,0x20821584u,0x20821588u,0x2082158cu,0x20821590u,0x20821680u,0x20821684u,0x20821688u,0x2082168cu,0x20821690u,0x20821700u,0x20821704u,0x20821708u,0x2082170cu,0x20821710u,0x20821800u,0x20821804u,0x20821808u,0x2082180cu,0x20821810u,0x20821900u,0x20821904u,0x20821908u,0x2082190cu,0x20821910u,0x20821a00u,0x20821a04u,0x20821a08u,0x20821a0cu,0x20821a10u,0x20821780u,0x20821784u,0x20821788u,0x2082178cu,0x20821790u,0x20821880u,0x20821884u,0x20821888u,0x2082188cu,0x20821890u,0x20821980u,0x20821984u,0x20821988u,0x2082198cu,0x20821990u,0x20821a80u,0x20821a84u,0x20821a88u,0x20821a8cu,0x20821a90u,0x20821000u,0x20821048u,0x20821114u,0x20821214u,0x20810308u,0x20810310u,0x20810314u,0x20810128u};
struct vp_bus_record {
 uint32_t sequence,elapsed_ms,status,phase,driver,scene,preset,sequence_before,sequence_after;
 uint32_t valid[VP_BUS_MASKS],changed[VP_BUS_MASKS],value[VP_BUS_REGS],commit;
};
struct vp_bus_ring {char magic[8];uint32_t schema,record_bytes,capacity,total,phase_mask,reserved;struct vp_bus_record record[3];};
static void vp_bus_init(struct vp_bus_ring*p){memset(p,0,sizeof*p);memcpy(p->magic,"NXLBUS1",8);p->schema=1;p->record_bytes=sizeof(struct vp_bus_record);p->capacity=3;}
static void vp_bus_capture(const struct vp_reader*r,struct vp_bus_ring*p,const struct vp_record*s,uint32_t milliseconds,unsigned phase){
 if(phase>2u)return;
 struct vp_bus_record z;memset(&z,255,sizeof z);memset(z.valid,0,sizeof z.valid);memset(z.changed,0,sizeof z.changed);
 z.sequence=p->total;z.elapsed_ms=milliseconds;z.status=1;z.phase=phase;z.driver=s->rtos[0];z.scene=s->rtos[1];z.preset=s->rtos[2];
 uint32_t d2,scene2,preset2;
 if(!vp_rw(r,0x817dbbe8u,&z.sequence_before))z.status=3;
 for(unsigned i=0;i<VP_BUS_REGS;i++){
  uint32_t a,b;if(!vp_rw(r,vp_bus_address[i],&a)||!vp_rw(r,vp_bus_address[i],&b)){z.status=3;continue;}
  z.value[i]=a;z.valid[i/32]|=1u<<(i%32);if(a!=b){z.changed[i/32]|=1u<<(i%32);if(z.status==1)z.status=2;}
 }
 if(!vp_rw(r,0x817dbbe8u,&z.sequence_after)||!vp_rw(r,0x8146e438u,&d2)||!vp_rw(r,0x813f1b30u,&scene2)||!vp_rw(r,0x813ebf60u,&preset2))z.status=3;
 else if(d2!=z.driver||scene2!=z.scene||preset2!=z.preset)z.status=4;
 z.commit=vp_hash(&z,offsetof(struct vp_bus_record,commit))^VP_BUS_XOR;
 struct vp_bus_record*out=p->record+phase;out->commit=0;__sync_synchronize();memcpy(out,&z,offsetof(struct vp_bus_record,commit));__sync_synchronize();out->commit=z.commit;__sync_synchronize();
 if(phase==1u)p->phase_mask&=~4u;p->phase_mask|=1u<<phase;p->total++;
}
_Static_assert(sizeof(struct vp_bus_record)==624,"fixed DMA snapshot");
_Static_assert(VP_BUS_OFFSET==2048u&&VP_BUS_OFFSET+sizeof(struct vp_bus_ring)<=VP_HEADER_BYTES,"DMA uses only existing header padding");
#endif
