/* SPDX-License-Identifier: AGPL-3.0-only. R11c dense output-tail snapshots.
 * Only an active, typed recording path permits device/RAM reads. Native
 * A/B transitions are kept RAW; a transition is not same-request EOF.
 * No old-buffer pointer cache, target call, device write, ACK or selector. */
#ifndef NXL_VIDEO_TAIL_CAPTURE_H
#define NXL_VIDEO_TAIL_CAPTURE_H
#define VT_KIND 9u
static uint32_t vt_pixel_address(uint32_t y,uint32_t uv,unsigned i){
 static const unsigned order[6]={2,6,4,3,7,5};return vz_pixel_address(y,uv,order[i]);
}
static int vt_ids(const struct vp_reader*r,const struct bv_state*b,uint32_t*d){
 return bv_identity(r,d)&&d[0]==b->camera&&d[2]==b->source&&d[4]==2u&&d[7]==r->modules+0x4c9c8u;
}
static void vt_word(const struct vp_reader*r,struct bv_record*z,unsigned field,uint32_t address,unsigned bit){
 if(vp_rw(r,address,z->d+field))z->d[95]|=1u<<bit;
}
static void vt_block(const struct vp_reader*r,struct bv_record*z,unsigned field,uint32_t address,unsigned n,unsigned bit){
 if(r->physical(r->context,address,z->d+field,n))z->d[95]|=1u<<bit;
}
static void vt_capture(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 struct bv_state*b,const struct vz_io*io,uint64_t(*now)(void*),void*c,uint32_t anchor){
 if(h->w[4]>=BV_CAPACITY){h->w[6]|=1u;return;}
 struct bv_record z;memset(&z,0,sizeof z);memset(z.d,255,sizeof z.d);z.kind=VT_KIND;z.window=b->window;z.begin_ns=now(c);
 if(pl_read_header(r,z.d))z.flags|=1u;
 if(!(z.flags&1u)||vp_timer_schema(z.d)!=11u||!vp_timer_inflight(z.d)||!(z.d[3]&512u)){
  z.end_ns=now(c);bv_append(h,records,&z);return;
 }
 z.d[94]=z.d[95]=z.d[96]=0;z.d[97]=1;z.d[129]=0;
 z.d[130]=r->modules;z.d[131]=r->core;z.d[132]=b->camera;z.d[133]=b->source;z.d[135]=anchor;z.d[136]=4;
 if(vt_ids(r,b,z.d+42)){
  z.d[96]|=1u;z.d[134]=z.d[48];
  if(bv_hal(r,z.d[48],z.d+58,z.d+66))z.d[96]|=4u;
  vt_block(r,&z,32,0x20821900u,20u,0);vt_word(r,&z,98,0x2082110cu,2);
  vt_block(r,&z,100,0x208210d0u,12u,4);vt_block(r,&z,106,0x817d8ab4u,12u,6);
  vt_word(r,&z,112,0x208210c8u,8);vt_word(r,&z,114,0x817d8aacu,10);
  vt_block(r,&z,116,0x81459970u,8u,12);vt_word(r,&z,120,0x20810908u,14);
  vt_word(r,&z,122,0x8146e438u,16);vt_word(r,&z,124,0x813ebf60u,18);
  vt_word(r,&z,126,0x208210b0u,20);
  if((z.d[95]&(1u<<20))&&z.d[126]==2u)vt_word(r,&z,128,0x208210e0u,22);
  if((z.d[95]&5u)==5u){
   if(((z.d[98]>>8)&15u)==7u&&((z.d[34]>>12)&0x7ffffu)==3840u&&io&&io->pixel){
    for(unsigned i=0;i<6;i++){
     uint32_t bytes[4],p=vt_pixel_address(z.d[32],z.d[33],i);z.d[82+i]=p;
     if(vz_pixel_allowed(z.d[32],z.d[33],p,16u)&&io->pixel(io->context,z.d[32],z.d[33],p,bytes,16u)){
      z.d[88+i]=vp_hash(bytes,16u);z.d[94]|=1u<<i;
     }
    }
   }
  }
 }
 /* Failed/replaced recording roots suppress all second-pass MMIO. Only
  * the final native RAM header is still captured after such a failure. */
 if(vt_ids(r,b,z.d+50)){
  z.d[96]|=2u;
  if(bv_hal(r,z.d[56],z.d+62,z.d+74))z.d[96]|=8u;
  if((z.d[96]&3u)==3u&&vz_ids_same(z.d+42,z.d+50)){
   vt_block(r,&z,37,0x20821900u,20u,1);vt_word(r,&z,99,0x2082110cu,3);
   vt_block(r,&z,103,0x208210d0u,12u,5);vt_block(r,&z,109,0x817d8ab4u,12u,7);
   vt_word(r,&z,113,0x208210c8u,9);vt_word(r,&z,115,0x817d8aacu,11);
   vt_block(r,&z,118,0x81459970u,8u,13);vt_word(r,&z,121,0x20810908u,15);
   vt_word(r,&z,123,0x8146e438u,17);vt_word(r,&z,125,0x813ebf60u,19);
   vt_word(r,&z,127,0x208210b0u,21);
  }
 }
 z.d[129]=(z.d[95]&0x700000u)==0x700000u&&z.d[126]==2u&&z.d[127]==2u;
 if(pl_read_header(r,z.d+16))z.flags|=2u;
 z.end_ns=now(c);bv_append(h,records,&z);
}
#endif
