/* SPDX-License-Identifier: AGPL-3.0-only. Optional passive path snapshots.
 * Fits in the already allocated header padding: sample ABI and RAM allocation
 * stay unchanged. One snapshot/second in E005; no MMIO or target writes.
 */
#ifndef NXL_VIDEO_PATH_CAPTURE_H
#define NXL_VIDEO_PATH_CAPTURE_H
#include "video_counter_capture.h"
#define VP_PATH_OFFSET 1024u
#define VP_PATH_CAPACITY 3u
#define VP_PATH_XOR 0x75b3a129u
struct vp_path_record {uint32_t word[64];};
struct vp_path_ring {
 char magic[8];uint32_t schema,record_bytes,capacity,total,reserved[2];
 struct vp_path_record record[VP_PATH_CAPACITY];
};
static int vp_path_ram(uint32_t a,unsigned n){return !(a&3u)&&a>=0x81006000u&&(uint64_t)a+n<=0x94000000ULL;}
static int vp_path_read(const struct vp_reader*r,uint32_t a,void*out,unsigned n){return vp_path_ram(a,n)&&r->physical(r->context,a,out,n);}
static void vp_path_init(struct vp_path_ring*p){memset(p,0,sizeof*p);memcpy(p->magic,"NXLPATH1",8);p->schema=2;p->record_bytes=sizeof(struct vp_path_record);p->capacity=VP_PATH_CAPACITY;}
static void vp_path_capture(const struct vp_reader*r,struct vp_path_ring*p,const struct vp_record*s,uint32_t milliseconds){
 struct vp_path_record rec;memset(&rec,255,sizeof rec);uint32_t*w=rec.word;
 w[0]=p->total;w[1]=milliseconds;w[2]=s->rtos[0];w[3]=s->rtos[2];w[4]=0;w[5]=s->rtos[3];
 uint32_t type=0,active=0,again=0,desc[6],desc2[6],global=0,buf[20],verify[20],preset=0;
 if(vp_rw(r,0x8153cbecu,&type)&&type==0x80592110u&&vp_rw(r,0x8153d964u,&active)&&vp_path_read(r,active,&type,4)&&(type==0x80592268u||type==0x80592328u)&&vp_path_read(r,0x8153d928u,desc,sizeof desc)){
  w[6]=active;w[7]=type;w[9]=desc[0];w[10]=desc[1];w[11]=desc[2];w[12]=desc[4];
  if(vp_rw(r,0x8153d910u,w+8))w[4]|=1;
  if(vp_rw(r,0x81543d34u,&global)&&vp_path_read(r,global,buf,56)&&vp_path_read(r,global,verify,56)&&!memcmp(buf,verify,56)&&vp_rw(r,0x81543d34u,&again)&&again==global){w[13]=global;memcpy(w+14,buf,56);w[4]|=2;}
  if(vp_path_read(r,desc[2],buf,80)&&buf[0]==0x80588910u&&vp_path_read(r,desc[2],verify,80)&&!memcmp(buf,verify,80)){memcpy(w+28,buf,80);w[4]|=4;}
  if(vp_path_read(r,desc[4],buf,48)&&buf[0]==0x80588910u&&vp_path_read(r,desc[4],verify,48)&&!memcmp(buf,verify,48)){memcpy(w+48,buf,48);w[4]|=8;}
  if(!vp_path_read(r,0x8153d928u,desc2,sizeof desc2)||memcmp(desc,desc2,sizeof desc)||!vp_rw(r,0x8153d964u,&again)||again!=active||!vp_path_read(r,active,&again,4)||again!=type)w[4]|=0x80000000u;
 }
 /* Independent native backend states; decode only after exact type checks. */
 if(vp_rw(r,0x8142e018u,&type)&&type==0x80582ba8u&&vp_rw(r,0x8142ed3cu,w+60))w[4]|=16;
 if(vp_rw(r,0x81430238u,&type)&&type==0x80583228u&&vp_rw(r,0x81430f5cu,w+61))w[4]|=32;
 if(!vp_rw(r,0x813ebf60u,&preset)||preset!=w[3])w[4]|=0x80000000u;
 /* Version 2 uses the reserved word for four native RAM policy bytes:
  * UD flag, QHD flag, LDC op[0x1d], LDC op[0x1e]. No MMIO read is added. */
 w[62]=0xffffffffu;uint32_t policy[2],policy2[2],op=0,op2=0,opword=0,opword2=0;
 if(vp_path_read(r,0x8142d0d8u,policy,8)&&vp_path_read(r,0x8142d0d8u,policy2,8)&&!memcmp(policy,policy2,8)&&policy[0]<=1u&&policy[1]<=1u){w[62]=(w[62]&0xffff0000u)|policy[0]|(policy[1]<<8);w[4]|=64;}
 if((w[4]&16u)&&vp_rw(r,0x8142f1e8u,&op)&&vp_path_ram(op,84)&&vp_path_read(r,op+0x1cu,&opword,4)&&vp_path_read(r,op+0x1cu,&opword2,4)&&opword==opword2&&vp_rw(r,0x8142f1e8u,&op2)&&op==op2&&vp_rw(r,0x8142e018u,&type)&&type==0x80582ba8u){w[62]=(w[62]&65535u)|((opword&0x00ffff00u)<<8);w[4]|=128;}
 w[63]=vp_hash(w,252)^VP_PATH_XOR;
 struct vp_path_record*out=p->record+(p->total%VP_PATH_CAPACITY);
 out->word[63]=0;__sync_synchronize();memcpy(out,&rec,252);__sync_synchronize();out->word[63]=w[63];__sync_synchronize();p->total++;
}
_Static_assert(sizeof(struct vp_path_record)==256,"fixed path snapshot");
_Static_assert(VP_PATH_OFFSET+sizeof(struct vp_path_ring)<=VP_HEADER_BYTES,"path snapshots fit existing padding");
_Static_assert(sizeof(struct vp_header)<=VP_PATH_OFFSET,"path snapshots do not overlap header");
#endif
