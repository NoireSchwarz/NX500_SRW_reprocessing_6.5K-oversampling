/* SPDX-License-Identifier: AGPL-3.0-only. Typed RAM metadata only; no planes. */
#ifndef NXL_EP_CORE_H
#define NXL_EP_CORE_H
#include "still_core.h"
#define P35_EP 0x8142d154u
#define P35_STILL 0x8142df3cu
#define P35_EP_BASE 131u
#define P35_BLOCK_BASE 186u
#define P35_BLOCK_WORDS 249u
#define P35_WORDS (P35_BLOCK_BASE+6u*P35_BLOCK_WORDS)
#define P35_DURATION_MS 60000u
#define P35_INTERVAL_US 4000u
#define P35_MAX_RECORDS 1024u
/* IPC words0..106 retain P34 layout. Words107..118 /119..130 extend
 * each verified image from128 to176 bytes. EP55 words: identity,state,
 * selected/view/still/cs pointers,new-param flag,6 pending counts,2x9-word
 * requests,selected vptr,22-word fixed Still object,follow status.
 * Block249 words: param ptr,ctrl ptr,ctrl vptr,param state,count,14 param
 * words; 5 image slots of {state,pointer,44 header words}. Resize slots:
 * source,outputs0..3. LDC/NOG/3DLUT: source,destination. MC: destination
 * only at slot1 (field+24); no guessing of its input array layout.
 */
struct p35_snapshot {uint32_t w[P35_WORDS];};
struct p35_record {uint32_t us,sequence,flags;struct p35_snapshot data;uint32_t crc;};
static int p35_image(p34_read_fn rd,void *ctx,uint32_t p,uint32_t *slot){
 slot[1]=p;if(!p)return 1;
 if(!p34_ram_range(p,176)){slot[0]=2;return 1;}
 if(!rd(ctx,p,slot+2,1))return 0;
 if(slot[2]!=0x80588910u){slot[0]=3;return 1;}
 if(!rd(ctx,p,slot+2,32)||!rd(ctx,p+128,slot+34,12))return 0;
 if(slot[2]!=0x80588910u){slot[0]=3;return 1;}slot[0]=1;return 1;
}
static unsigned p35_param_words(uint32_t v){
 switch(v){case 0x80711f10u:return 4;case 0x80713a28u:return 10;
 case 0x80713e70u:return 6;case 0x80714990u:return 10;
 case 0x80711f20u:return 14;case 0x80714e60u:return 7;default:return 0;}
}
static int p35_block(p34_read_fn rd,void *ctx,uint32_t p,uint32_t ctrl,uint32_t *b){
 b[0]=p;b[1]=ctrl;
 if(ctrl&&p34_ram_range(ctrl,4)&&!rd(ctx,ctrl,b+2,1))return 0;
 if(!p)return 1;
 if(!p34_ram_range(p,4)){b[3]=2;return 1;}
 if(!rd(ctx,p,b+5,1))return 0;
 unsigned count=p35_param_words(b[5]);if(!count){b[3]=3;return 1;}
 if(!p34_ram_range(p,count*4)){b[3]=2;return 1;}
 if(!rd(ctx,p,b+5,count))return 0;
 if(p35_param_words(b[5])!=count){b[3]=3;return 1;}
 b[3]=1;b[4]=count;
 for(unsigned k=0;k<5;k++)b[19+46*k]=5; /* not used by this type */
 uint32_t v=b[5];unsigned offsets[5]={0},n=0;
 if(v==0x80711f20u){offsets[0]=12;for(unsigned k=1;k<5;k++)offsets[k]=7+k;n=5;}
 else if(v==0x80713a28u||v==0x80713e70u||v==0x80714990u){offsets[0]=4;offsets[1]=5;n=2;}
 else if(v==0x80714e60u){if(!p35_image(rd,ctx,b[5+6],b+19+46))return 0;}
 for(unsigned k=0;k<n;k++)if(!p35_image(rd,ctx,b[5+offsets[k]],b+19+46*k))return 0;
 return 1;
}
static int p35_capture(p34_read_fn rd,void *ctx,struct p35_snapshot *s){
 memset(s,0,sizeof *s);struct p34_snapshot ipc;int rc=p34_capture(rd,ctx,&ipc);
 if(rc!=1)return rc;memcpy(s->w,ipc.w,sizeof ipc);uint32_t *w=s->w,*e=w+P35_EP_BASE;
 for(unsigned k=0;k<2;k++){
  unsigned st=k?73:40,ptr=k?17:15,tail=k?119:107;
  if(w[st]==1){if(!p34_ram_range(w[ptr],176)){w[st]=2;continue;}
   if(!rd(ctx,w[ptr]+128,w+tail,12))return -1;}
 }
 if(!rd(ctx,P35_EP,e,1))return -1;if(!e[0])return 1;
 if(e[0]!=0x80582820u)return 0;
 if(!rd(ctx,P35_EP+3364,e+1,1)||!rd(ctx,P35_EP+3444,e+2,5)||
 !rd(ctx,P35_EP+3284,e+7,6)||!rd(ctx,P35_EP+3372,e+13,18))return -1;
 if(e[2]&&p34_ram_range(e[2],4)&&!rd(ctx,e[2],e+31,1))return -1;
 if(!rd(ctx,P35_STILL,e+32,1))return -1;
 if(e[32]==0x805829d8u){if(!rd(ctx,P35_STILL,e+32,22))return -1;}
 /* Retained request pointers are not followed after finalize. */
 unsigned state=e[1]&255u;
 if(state<2||state>4||e[2]!=P35_STILL||e[4]!=P35_STILL||e[31]!=0x805829d8u||e[32]!=0x805829d8u)return 1;
 const uint32_t *q=e+22;
 if(q[0]!=0x8072dd80u||!(q[7]&0x20000000u)||(q[7]&0x90000000u)){e[54]=2;return 1;}
 if(q[8]>6){e[54]=3;return 1;}
 e[54]=1;
 for(unsigned k=0;k<q[8];k++)if(!p35_block(rd,ctx,q[1+k],e[34+k],w+P35_BLOCK_BASE+k*P35_BLOCK_WORDS))return -1;
 return 1;
}
static int p35_stable(p34_read_fn rd,void *ctx,struct p35_snapshot *s){
 struct p35_snapshot a,b;int x=p35_capture(rd,ctx,&a),y=p35_capture(rd,ctx,&b);
 if(x<0||y<0)return -1;if(!x||!y||memcmp(&a,&b,sizeof a))return 0;*s=b;return 1;
}
static int p35_ipc_active(const struct p35_snapshot *s){struct p34_snapshot p;memcpy(p.w,s->w,sizeof p);return p34_native_active(&p);}
static int p35_ep_active(const struct p35_snapshot *s){return s->w[P35_EP_BASE+54]==1&&s->w[P35_EP_BASE+30]>0;}
static unsigned p35_image_count(const struct p35_snapshot *s){unsigned n=0;for(unsigned k=0;k<6;k++)for(unsigned j=0;j<5;j++)n+=s->w[P35_BLOCK_BASE+k*P35_BLOCK_WORDS+19+46*j]==1;return n;}
#endif
