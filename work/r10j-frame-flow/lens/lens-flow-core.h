/* SPDX-License-Identifier: AGPL-3.0-only. Metadata observation, no coefficient
 * decoding and no image planes. Mirrors the pinned original EP/Still types. */
#ifndef NX_LENS_FLOW_CORE_H
#define NX_LENS_FLOW_CORE_H
#include <stdint.h>
#include <string.h>
#define LF_EP 0x8142d154u
#define LF_STILL 0x8142df3cu
#define LF_LDC_OWNER (0x8142efc8u+548u)
#define LF_LINKS 8u
typedef int(*lf_read)(void*,uint32_t,void*,unsigned);
struct lf_link {uint32_t source_word,pointer,bytes,available;uint32_t data[128];};
struct lf_sample {uint32_t schema,status,stable,ep_vptr,ep_state,selection[5],counts[6],requests[18],still_vptr,controls[6],parameter_vptr[6],control_vptr[6],type_available[6],ldc_count,ldc_pointer,ldc_available,ldc[27],ldc_parameters[6][10],ldc_controls[6][6],links_count;struct lf_link link[LF_LINKS];uint32_t end_state,end_selection[5],end_ldc_pointer,end_request[9],end_controls[6],end_vptr[2];};
static int lf_ram(uint32_t p,unsigned n){return p>=0x81000000u&&!(p&3u)&&n&&n<=4096&&(uint64_t)p+n<=0x94000000ULL;}
static int lf_words(lf_read read,void*ctx,uint32_t p,void*out,unsigned words){return lf_ram(p,words*4u)&&read(ctx,p,out,words*4u);}
static int lf_capture(lf_read read,void*ctx,struct lf_sample*s){
 memset(s,0,sizeof *s);s->schema=2;
 if(!lf_words(read,ctx,LF_EP,&s->ep_vptr,1)||!lf_words(read,ctx,LF_EP+3364,&s->ep_state,1)||!lf_words(read,ctx,LF_EP+3444,s->selection,5)||!lf_words(read,ctx,LF_EP+3284,s->counts,6)||!lf_words(read,ctx,LF_EP+3372,s->requests,18)||!lf_words(read,ctx,LF_STILL,&s->still_vptr,1))return -1;
 if(!s->ep_vptr){s->status=0;return 1;}if(s->ep_vptr!=0x80582820u){s->status=2;return 1;}
 unsigned state=s->ep_state&255u;uint32_t*q=s->requests+9;
 if(state<2||state>4||s->selection[0]!=LF_STILL||s->selection[2]!=LF_STILL||s->still_vptr!=0x805829d8u||q[0]!=0x8072dd80u||!(q[7]&0x20000000u)||(q[7]&0x90000000u)||q[8]>6){s->status=1;return 1;}
 if(!lf_words(read,ctx,LF_STILL+8,s->controls,6))return -1;s->status=3;
 for(unsigned k=0;k<q[8];k++){
  if(lf_words(read,ctx,q[1+k],s->parameter_vptr+k,1))s->type_available[k]|=1;
  if(lf_words(read,ctx,s->controls[k],s->control_vptr+k,1))s->type_available[k]|=2;
  if(s->type_available[k]!=3)continue;
  if(s->parameter_vptr[k]!=0x80713a28u||s->control_vptr[k]!=0x80582da8u)continue;
  if(!lf_words(read,ctx,q[1+k],s->ldc_parameters[k],10)||!lf_words(read,ctx,s->controls[k],s->ldc_controls[k],6))return -1;
  if(s->ldc_parameters[k][0]!=0x80713a28u||s->ldc_controls[k][0]!=0x80582da8u)return 0;s->ldc_count++;
 }
 if(s->ldc_count){if(!lf_words(read,ctx,LF_LDC_OWNER,&s->ldc_pointer,1))return -1;if(lf_ram(s->ldc_pointer,sizeof s->ldc)){
  s->ldc_available=(unsigned)lf_words(read,ctx,s->ldc_pointer,s->ldc,27);
  static const unsigned order[27]={19,23,18,22,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,20,21,24,25,26};
  for(unsigned j=0;s->ldc_available&&j<27&&s->links_count<LF_LINKS;j++){unsigned i=order[j];uint32_t p=s->ldc[i];if(!lf_ram(p,512)||p==s->ldc_pointer)continue;unsigned seen=0;for(unsigned n=0;n<s->links_count;n++)seen|=s->link[n].pointer==p;if(seen)continue;
   struct lf_link*l=s->link+s->links_count++;l->source_word=i;l->pointer=p;l->bytes=sizeof l->data;l->available=(unsigned)lf_words(read,ctx,p,l->data,128);
  }
 }}
 if(!lf_words(read,ctx,LF_EP+3364,&s->end_state,1)||!lf_words(read,ctx,LF_EP+3444,s->end_selection,5)||!lf_words(read,ctx,LF_LDC_OWNER,&s->end_ldc_pointer,1)||!lf_words(read,ctx,LF_EP+3372+36,s->end_request,9)||!lf_words(read,ctx,LF_STILL+8,s->end_controls,6)||!lf_words(read,ctx,LF_EP,s->end_vptr,1)||!lf_words(read,ctx,LF_STILL,s->end_vptr+1,1))return -1;
 s->stable=s->end_state==s->ep_state&&!memcmp(s->end_selection,s->selection,20)&&s->end_vptr[0]==s->ep_vptr&&s->end_vptr[1]==s->still_vptr&&!memcmp(s->end_request,q,36)&&!memcmp(s->end_controls,s->controls,24)&&(!s->ldc_count||s->end_ldc_pointer==s->ldc_pointer);return s->stable?1:0;
}
#endif
