/* Read-only BayerStill -> LDC boundary evidence. Native photo mode normally
 * uses OTF (zero DMA plane addresses): record that fact, never stale pixels.
 * Only a retired, stable 8-bit NV16 RAM output identical to the validated
 * module DMA contract is eligible for bounded plane reads. */
#include "../common/pixel_sample.h"
struct native_pixel_record {uint32_t schema,cycle,still_state,ep_state,param_pointer,source_pointer,params[10],image[44],dma[4];struct ps_sample sample;};
static unsigned native_pixel_count,native_pixel_cycle,native_pixel_active,native_pixel_status;
static struct native_pixel_record native_pixels;
static int native_pixel_read(struct ps_sample*s){
 unsigned char again[PS_SIDE];
 for(unsigned k=0;k<PS_POINTS;k++)for(unsigned c=0;c<2;c++)for(unsigned y=0;y<PS_SIDE;y++){
  uint64_t at=(uint64_t)(c?s->source_uv:s->source_y)+(s->xy[k][1]+y)*s->stride+s->xy[k][0];unsigned char*p=s->tiles[k][c]+y*PS_SIDE;
  if(at<0x94000000u||at+PS_SIDE>0xbb6cc000u||pread(dev,p,PS_SIDE,(off_t)at)!=PS_SIDE||pread(dev,again,PS_SIDE,(off_t)at)!=PS_SIDE||memcmp(p,again,PS_SIDE))return 0;
 }return 1;
}
static void native_pixel_sample(unsigned sequence,const struct p36_snapshot*s){
 if(native_pixel_count>=64)return;unsigned state=s->base.w[1]&255u,active=state>=2&&state<=6;
 if(active&&!native_pixel_active){native_pixel_cycle++;native_pixel_status=0;}native_pixel_active=active;
 if(!p36_ep_active(s)||!native_pixel_cycle)return;
 const uint32_t*block=NULL;unsigned block_index=0;
 for(unsigned k=0;k<s->base.w[P35_EP_BASE+30];k++){
  const uint32_t*b=s->base.w+P35_BLOCK_BASE+k*P35_BLOCK_WORDS;
  if(b[3]==1&&b[5]==0x80713a28u&&b[2]==0x80582da8u&&b[19]==1){if(block)return;block=b;block_index=k;}
 }
 if(!block)return;memset(&native_pixels,0,sizeof native_pixels);struct native_pixel_record*r=&native_pixels;
 r->schema=1;r->cycle=native_pixel_cycle;r->still_state=state;r->ep_state=s->base.w[P35_EP_BASE+1]&255u;r->param_pointer=block[0];r->source_pointer=block[20];
 memcpy(r->params,block+5,40);memcpy(r->image,block+21,176);
 if(!mapped_read(0x817d7b90u,r->dma,16,1))return;
 struct ps_sample*p=&r->sample;ps_init(p,PS_BAYER_OUT,sequence,NULL);uint64_t begin=now();p->begin_sec=(uint32_t)(begin/1000000000u);p->begin_nsec=(uint32_t)(begin%1000000000u);
 p->source_y=r->dma[0];p->source_uv=r->dma[1];p->stride=r->dma[2];p->status=PS_UNSUPPORTED_LAYOUT;
 if(!r->dma[0]&&!r->dma[1]&&!r->image[27]&&!r->image[29])p->status=PS_OTF_NO_RAM;
 else if(state!=7)p->status=PS_NOT_RETIRED;
 else if(r->dma[0]>=0x94000000u&&r->dma[1]>r->dma[0]&&r->dma[2]==6496&&(r->dma[3]&0x7f3u)==0x41u&&
         r->image[3]==6480&&r->image[4]==4320&&r->image[36]>=2u*6496u*4320u&&r->image[27]==r->dma[0]&&r->image[29]==r->dma[1]&&
         r->dma[1]-r->dma[0]>=6496u*4320u&&(uint64_t)r->dma[1]+6496u*4320u<=0xbb6cc000u){
  p->uv_offset=p->source_uv-p->source_y;p->status=native_pixel_read(p)?PS_VALID:PS_CHANGED;
 }
 uint32_t mirror[4],header[44],again_state=0;
 if(!mapped_read(0x817d7b90u,mirror,16,1)||!mapped_read(r->source_pointer,header,176,1)||!word(P34_TASK+3364u,&again_state)||
    memcmp(mirror,r->dma,16)||memcmp(header,r->image,176)||again_state!=s->base.w[1])p->status=PS_CHANGED;
 if(p->status==PS_VALID){uint32_t ep_state=0,selected=0,param=0;
  if(!word(P35_EP+3364u,&ep_state)||ep_state!=s->base.w[P35_EP_BASE+1]||!word(P35_EP+3444u,&selected)||selected!=P35_STILL||
     !word(P35_EP+3412u+block_index*4,&param)||param!=r->param_pointer)p->status=PS_CHANGED;
 }
 if(p->status==native_pixel_status)return;
 uint64_t end=now();p->end_sec=(uint32_t)(end/1000000000u);p->end_nsec=(uint32_t)(end%1000000000u);ps_seal(p);
 if(frame(context_file,13,sequence,begin,end,r,sizeof *r)){native_pixel_status=p->status;native_pixel_count++;}
}
