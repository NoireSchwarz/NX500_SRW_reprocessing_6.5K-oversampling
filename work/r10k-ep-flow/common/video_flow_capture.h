/* SPDX-License-Identifier: AGPL-3.0-only. Bounded passive flow context.
 * Parameters remain raw unless an OEM consumer established their meaning.
 * No per-frame reads, target calls, IRQ ACK or shared timer reads on Linux.
 */
#ifndef NXL_VIDEO_FLOW_CAPTURE_H
#define NXL_VIDEO_FLOW_CAPTURE_H
#define VP_FLOW 524288u
static int vp_video_flow_mode(const struct vp_record*s){
 uint32_t d=s->rtos[0]&65535u;
 return (d==0x1717u||d==0x1818u||d==0x1919u)&&
        (s->rtos[2]==0xe004u||s->rtos[2]==0xe005u);
}
static void vp_flow_capture(const struct vp_reader*r,struct vp_record*s){
 uint32_t*z=s->flow,v[10],again[10],hal=s->hevc[0],table[8],p=0,q=0,g[14],g2[14];
 memset(z,255,sizeof s->flow);z[0]=1u;z[1]=0;
 if(!vp_video_flow_mode(s)||(s->valid&VP_CHANGED))return;
 if((s->valid&VP_HEVC)&&vp_block(r,hal+0x5cu,v,sizeof v)&&vp_block(r,hal+0x5cu,again,sizeof again)&&
    !memcmp(v,again,sizeof v)&&vp_type(r,hal,r->modules+0x4c9c8u)){
  z[2]=v[2];z[3]=v[3];z[4]=v[9];z[5]=v[7];z[6]=v[4];z[7]=v[8];z[8]=v[0];z[9]=v[1];z[1]|=1u;
 }
 if(r->physical(r->context,0x813f09b4u,table,sizeof table)){
  z[10]=table[3];z[11]=table[6];z[12]=table[2];z[13]=table[7];z[1]|=2u;
 }
 if(s->valid&VP_BACKEND){
  z[14]=s->hevc[9];
  if(vp_rw(r,0x81543d34u,&p)&&vp_path_read(r,p,g,sizeof g)&&vp_path_read(r,p,g2,sizeof g2)&&
     !memcmp(g,g2,sizeof g)&&vp_rw(r,0x81543d34u,&q)&&p==q){z[15]=p;memcpy(z+16,g,sizeof g);z[1]|=4u;}
 }
 if(s->valid&VP_SOURCE){z[30]=s->source[5];z[1]|=8u;}
 if(vp_rw(r,0x8153d910u,z+31))z[1]|=16u;
 s->valid|=VP_FLOW;
}
#endif
