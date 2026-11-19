/* SPDX-License-Identifier: AGPL-3.0-only.
 * Read-only NX500 1.12 software parameters. Stable repeated RAM values are
 * NOT an atomic frame snapshot or proof of hardware/DMA consumption. */
#ifndef NXL_COLOR_OBSERVATION_H
#define NXL_COLOR_OBSERVATION_H
#include <stdint.h>
#include <string.h>
#include "native_gamma.h"
#define CO_MAGIC 0x314c4f43u
#define CO_COLOR_VPTR 0x8062fd60u
#define CO_PARENT_ALT 0x8063e348u
#define CO_PERIOD_MS 500u
#define CO_LIMIT 600u
enum {CO_OK=1,CO_IDENTITY=2,CO_READ=3,CO_CHANGED=4,CO_RANGE=5,CO_SHAPE=6,CO_CHAIN=7};
struct co_channel {
 uint32_t status,parent,gamma,color,context,parent_head[6],parent_gamma;
 uint32_t gamma_head[8],color_head[8],gamma_widths[5],color_widths[5];
 uint32_t gamma_descriptor,color_descriptor,gamma_tail[78],color_tail[31],input[49];
};
struct co_payload {
 uint32_t manager[5],roots[4],mapping[3],driver[2],callbacks[4],dynamic[9],binding_raw[24];
 struct co_channel channel[2];uint32_t color_shadow[2][10],gamma_shadow[2][108];
};
struct co_record {
 uint32_t magic,schema,bytes,index,time_lo,time_hi,duration_us,status,media_busy;
 struct co_payload first,second;uint32_t camera_pid;char media_name[128];uint32_t checksum,commit;
};
typedef int(*co_reader)(void*,uint32_t,void*,unsigned);
static int co_ram(uint32_t at,unsigned n){return n&&!(at&3u)&&at>=0x81006000u&&(uint64_t)at+n<=0x94000000ULL;}
static int co_read(co_reader r,void*c,uint32_t at,void*out,unsigned n){return co_ram(at,n)&&r(c,at,out,n);}
static int co_shape(co_reader r,void*c,uint32_t at,uint32_t type,const uint32_t widths[5],uint32_t head[8],uint32_t actual[5],unsigned n){
 if(!co_ram(at,n)||!co_read(r,c,at,head,32))return CO_READ;
 if(head[0]!=type||head[7]!=5)return CO_SHAPE;
 if(!co_read(r,c,head[6],actual,20))return CO_RANGE;
 return memcmp(actual,widths,20)?CO_SHAPE:CO_OK;
}
static int co_nodes(co_reader r,void*c,uint32_t parent,uint32_t*g,uint32_t*color){
 uint32_t h[8],visited[64],p;
 if(!co_read(r,c,parent,h,24))return CO_READ;
 p=h[1];*g=*color=0;unsigned n=0;
 while(p){
  if(n==64||!co_read(r,c,p,h,32)||h[3]!=parent)return CO_CHAIN;
  for(unsigned i=0;i<n;i++)if(visited[i]==p)return CO_CHAIN;visited[n++]=p;
  if(h[0]==GC_GAMMA_VPTR){if(*g)return CO_CHAIN;*g=p;}
  if(h[0]==CO_COLOR_VPTR){if(*color)return CO_CHAIN;*color=p;}p=h[5];
 }
 return *g&&*color?CO_OK:CO_CHAIN;
}
static int co_channel_read(co_reader r,void*c,struct co_channel*s,uint32_t parent,unsigned bank){
 s->parent=parent;
 if(!co_read(r,c,parent,s->parent_head,24)||!co_read(r,c,parent+0x170,&s->parent_gamma,4))return CO_READ;
 if((s->parent_head[0]!=GC_PARENT_VPTR&&s->parent_head[0]!=CO_PARENT_ALT)||((s->parent_head[3]>>8)&255u)!=bank)return CO_IDENTITY;
 int result=co_nodes(r,c,parent,&s->gamma,&s->color);if(result!=CO_OK)return result;
 if(s->gamma!=s->parent_gamma)return CO_CHAIN;
 const uint32_t gw[5]={3,5,4,13,2},cw[5]={2,2,2,2,7};
 result=co_shape(r,c,s->gamma,GC_GAMMA_VPTR,gw,s->gamma_head,s->gamma_widths,0xf74);if(result!=CO_OK)return result;
 result=co_shape(r,c,s->color,CO_COLOR_VPTR,cw,s->color_head,s->color_widths,0xeb8);if(result!=CO_OK)return result;
 if(s->gamma_head[3]!=parent||s->color_head[3]!=parent||s->gamma_head[2]!=s->color_head[2])return CO_IDENTITY;
 s->context=s->gamma_head[2];
 /* Native ParamField(object, input, field) receives obj[1] by VALUE.
  * It is a packed command descriptor, never a pointer to dereference. */
 s->gamma_descriptor=s->gamma_head[1];s->color_descriptor=s->color_head[1];
 if(!co_read(r,c,s->context,s->input,sizeof s->input))return CO_RANGE;
 if(!co_read(r,c,s->gamma+0xe3c,s->gamma_tail,sizeof s->gamma_tail)||!co_read(r,c,s->color+0xe3c,s->color_tail,sizeof s->color_tail))return CO_READ;
 return CO_OK;
}
static int co_payload_read(co_reader r,void*c,struct co_payload*s){
 memset(s,0,sizeof*s);
 if(!r(c,GC_MANAGER,s->manager,20)||!ic_roots(r,c,s->roots)||!r(c,0x81002edc,s->mapping,4)||!r(c,0x813ebf50,s->mapping+1,8))return CO_READ;
 if(s->manager[0]!=GC_MANAGER_VPTR||s->roots[0]!=0x80592640u||s->mapping[0]!=0xbb701402u||s->mapping[1]!=0xbb71b300u||s->mapping[2]!=0xbb71d300u)return CO_IDENTITY;
 if(!r(c,0x8146e438,s->driver,4)||!r(c,0x813ebf60,s->driver+1,4)||!r(c,0x810c3428,s->dynamic,36)||!r(c,0x817dbbe0,s->binding_raw,96))return CO_READ;
 const uint32_t slots[4]={0x8062f6c8,0x8062f600,0x8063e350,0x8062fd78};
 for(unsigned i=0;i<4;i++)if(!r(c,slots[i],s->callbacks+i,4))return CO_READ;
 for(unsigned i=0;i<2;i++)s->channel[i].status=co_channel_read(r,c,s->channel+i,s->manager[2+i],i);
 for(unsigned i=0;i<2;i++){uint32_t base=0xbb71d200u+i*0x2000u;if(!r(c,base,s->color_shadow[i],40)||!r(c,base+40,s->gamma_shadow[i],432))return CO_READ;}
 return s->channel[0].status;
}
static int co_capture(co_reader r,void*c,struct co_record*s){
 int a=co_payload_read(r,c,&s->first),b=co_payload_read(r,c,&s->second);
 s->status=(a!=CO_OK)?a:(b!=CO_OK)?b:memcmp(&s->first,&s->second,sizeof s->first)?CO_CHANGED:CO_OK;
 /* A stable bank0 sample is useful even when the inactive bank1 is absent;
  * its per-bank failure and partial raw values remain in both payloads. */
 return s->status==CO_OK;
}
static void co_commit(struct co_record*s){s->magic=CO_MAGIC;s->schema=1;s->bytes=sizeof*s;s->checksum=ic_hash(s,sizeof*s-8);s->commit=CO_MAGIC^s->checksum;}
static int co_valid(const struct co_record*s){return s->magic==CO_MAGIC&&s->schema==1&&s->bytes==sizeof*s&&s->status>=CO_OK&&s->status<=CO_CHAIN&&s->checksum==ic_hash(s,sizeof*s-8)&&s->commit==(CO_MAGIC^s->checksum);}
static int co_export_allowed(int usb,int media){return usb==0&&media==0;}
#endif
