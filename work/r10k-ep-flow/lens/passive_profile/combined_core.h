/* SPDX-License-Identifier: AGPL-3.0-only. Two observation groups, ordinary RAM. */
#ifndef NXL_COMBINED_CORE_H
#define NXL_COMBINED_CORE_H
#include "ep_core.h"
#define P36_DURATION_MS 60000u
#define P36_INTERVAL_US 4000u
#define P36_MAX_RECORDS 2048u
#define P36_KERNEL_DELTA 0x3a000000u
struct p36_blob {uint32_t state,pointer,words,data[21];};
struct p36_control {uint32_t state,pointer,words,data[44];};
struct p36_extra {
 uint32_t flags,rtos_masks[5],rtos_callbacks[21],registry[44],semaphore_ids[2],path_mirror[8],ipc_dma_mirror[74];
 uint32_t kernel[68]; /* availability,ACC mask,mutex3,21 completion triples */
 uint32_t iq_ready,iq_guard,iq_words[59]; /* fixed non-polymorphic IQ object */
 struct p36_blob ldc,lut,resize;
 struct p36_control control[3]; /* exact known LDC/LUT/Resize Still classes */
 uint32_t presets[6][17]; /* fixed address + first64 bytes, NOT whole LUTs */
 uint32_t inferred_lut_candidate;
};
struct p36_snapshot {struct p35_snapshot base;struct p36_extra extra;};
struct p36_record {uint32_t us,sequence,flags;struct p36_snapshot data;uint32_t crc;};
#define P36_WORDS (sizeof(struct p36_snapshot)/4u)
static int p36_words(p34_read_fn rd,void *ctx,uint32_t p,uint32_t *out,unsigned n){
 if(!p34_ram_range(p,n*4u))return 0;
 while(n){unsigned k=n>32?32:n;if(!rd(ctx,p,out,k))return 0;p+=k*4;out+=k;n-=k;}return 1;
}
static int p36_blob(p34_read_fn rd,void *ctx,uint32_t owner,unsigned n,struct p36_blob *b){
 if(!rd(ctx,owner,&b->pointer,1))return 0;if(!b->pointer)return 1;
 if(n>21||!p34_ram_range(b->pointer,n*4)){b->state=2;return 1;}
 if(!p36_words(rd,ctx,b->pointer,b->data,n))return 0;b->state=1;b->words=n;return 1;
}
static int p36_control(p34_read_fn rd,void *ctx,uint32_t p,uint32_t v,unsigned n,struct p36_control *b){
 b->pointer=p;if(!p)return 1;if(n>44||!p34_ram_range(p,n*4)){b->state=2;return 1;}
 if(!rd(ctx,p,b->data,1))return 0;if(b->data[0]!=v){b->state=3;return 1;}
 if(!p36_words(rd,ctx,p,b->data,n))return 0;
 if(b->data[0]!=v){b->state=3;return 1;}b->state=1;b->words=n;return 1;
}
static int p36_capture(p34_read_fn rd,void *ctx,int kernel_ok,struct p36_snapshot *s){
 memset(s,0,sizeof *s);int rc=p35_capture(rd,ctx,&s->base);if(rc!=1)return rc;
 struct p36_extra *x=&s->extra;
 if(!p36_words(rd,ctx,0x817d8aacu,x->rtos_masks,5)||!p36_words(rd,ctx,0x817d8c78u,x->rtos_callbacks,21)||
 !p36_words(rd,ctx,0x8153cb28u,x->registry,44)||!rd(ctx,0x8142d14cu,x->semaphore_ids,1)||!rd(ctx,0x81432f30u,x->semaphore_ids+1,1))return -1;
 x->flags=1;
 if(kernel_ok){
  if(!rd(ctx,0xc0755238u-P36_KERNEL_DELTA,x->kernel+1,1)||!rd(ctx,0xc071d81cu-P36_KERNEL_DELTA,x->kernel+2,3)||
  !p36_words(rd,ctx,0xc07554c8u-P36_KERNEL_DELTA,x->kernel+5,63))return -1;x->kernel[0]=1;
 }
 if(!p35_ep_active(&s->base)&&!p35_ipc_active(&s->base))return 1;
 if(!p36_words(rd,ctx,0x817d8a8cu,x->path_mirror,8)||!p36_words(rd,ctx,0x817d7b48u,x->ipc_dma_mirror,74))return -1;
 x->flags|=2;
 if(!rd(ctx,0x81792ab8u,&x->iq_guard,1))return -1;
 if(x->iq_guard&1){if(!p36_words(rd,ctx,0x81792abcu,x->iq_words,59))return -1;x->iq_ready=1;}
 if(!p35_ep_active(&s->base))return 1;
 for(unsigned i=0;i<s->base.w[P35_EP_BASE+30];i++){
  const uint32_t *b=s->base.w+P35_BLOCK_BASE+i*P35_BLOCK_WORDS;if(b[3]!=1)continue;
  if(b[5]==0x80713a28u&&b[2]==0x80582da8u){
   if(!p36_control(rd,ctx,b[1],0x80582da8u,6,x->control)||!p36_blob(rd,ctx,0x8142efc8u+548,21,&x->ldc))return -1;
  }else if(b[5]==0x80714990u&&b[2]==0x805838b8u){
   if(!p36_control(rd,ctx,b[1],0x805838b8u,6,x->control+1)||!p36_blob(rd,ctx,0x81433cccu,1,&x->lut))return -1;
  }else if(b[5]==0x80711f20u&&b[2]==0x805834a0u){
   if(!p36_control(rd,ctx,b[1],0x805834a0u,44,x->control+2))return -1;
   if(x->control[2].state==1&&!p36_blob(rd,ctx,b[1]+156,13,&x->resize))return -1;
  }
 }
 return 1;
}
static int p36_stable(p34_read_fn rd,void *ctx,int kernel_ok,struct p36_snapshot *s){
 struct p36_snapshot a,b;int x=p36_capture(rd,ctx,kernel_ok,&a),y=p36_capture(rd,ctx,kernel_ok,&b);
 if(x<0||y<0)return -1;if(!x||!y||memcmp(&a,&b,sizeof a))return 0;*s=b;return 1;
}
static int p36_ipc_active(const struct p36_snapshot *s){return p35_ipc_active(&s->base);}
static int p36_ep_active(const struct p36_snapshot *s){return p35_ep_active(&s->base);}
static unsigned p36_image_count(const struct p36_snapshot *s){return p35_image_count(&s->base);}
#endif
