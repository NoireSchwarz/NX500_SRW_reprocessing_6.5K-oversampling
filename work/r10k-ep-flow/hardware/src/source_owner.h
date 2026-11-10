/* SPDX-License-Identifier: AGPL-3.0-only; observations, not hardware ownership. */
#ifndef NXL_SOURCE_OWNER_H
#define NXL_SOURCE_OWNER_H
#include <stdint.h>
#include <string.h>
#define P10_SOURCE_BYTES 19712u
#define P10_POOL_LO 0x81006000u
#define P10_POOL_HI 0x813ee4c0u
#define P10_IMAGE_LO 0x80000000u
#define P10_IMAGE_HI 0x80b84280u
#define P10_OBSERVER_TABLE 0x8153cb28u
#define P10_OWNER_MAGIC 0x304f584eu
enum p10_domain {P10_SOURCE,P10_FIXED,P10_OBJECT,P10_IMAGE};
typedef int (*p10_reader)(void*,enum p10_domain,uint32_t,void*,uint32_t);
struct p10_owner_entry {uint32_t selector,present,object_pa,object[8],vtable_pa,vtable[4],notify_fn,code[8];};
struct p10_owner {uint32_t magic,version,bytes,count,table_pa,object_bytes,code_bytes,reserved,table[4];struct p10_owner_entry entry[2];};
static int p10_range(uint32_t address,uint32_t bytes,uint32_t lo,uint32_t hi){return bytes&&address>=lo&&(uint64_t)address+bytes<=hi;}
static int p10_source_pa_valid(uint32_t p){return !(p&255)&&p10_range(p,P10_SOURCE_BYTES,P10_POOL_LO,P10_POOL_HI);}
static int p10_read_allowed(enum p10_domain domain,uint32_t address,uint32_t bytes){
 if(domain==P10_SOURCE)return bytes==P10_SOURCE_BYTES&&p10_source_pa_valid(address);
 if(domain==P10_FIXED)return address==P10_OBSERVER_TABLE&&bytes==16;
 if(domain==P10_OBJECT)return bytes==32&&!(address&3)&&p10_range(address,bytes,0x81000000u,0x86000000u);
 if(domain==P10_IMAGE)return (bytes==16||bytes==32)&&!(address&1)&&p10_range(address,bytes,P10_IMAGE_LO,P10_IMAGE_HI);
 return 0;
}
static int p10_owner_capture(struct p10_owner *o,void *context,p10_reader read){
 memset(o,0,sizeof *o);o->magic=P10_OWNER_MAGIC;o->version=1;o->bytes=sizeof *o;o->count=2;o->table_pa=P10_OBSERVER_TABLE;o->object_bytes=o->code_bytes=32;
 if(!read(context,P10_FIXED,o->table_pa,o->table,16))return 0;
 for(unsigned i=0;i<2;i++){
  struct p10_owner_entry *e=o->entry+i;e->selector=7+i;e->object_pa=o->table[i?2:3];
  if(!e->object_pa)continue;
  if(!p10_read_allowed(P10_OBJECT,e->object_pa,32)||!read(context,P10_OBJECT,e->object_pa,e->object,32))return 0;
  e->vtable_pa=e->object[0];if(e->vtable_pa&3||!p10_read_allowed(P10_IMAGE,e->vtable_pa,16)||!read(context,P10_IMAGE,e->vtable_pa,e->vtable,16))return 0;
  e->notify_fn=e->vtable[2];uint32_t pc=e->notify_fn&~1u;
  if(!e->notify_fn||(!(e->notify_fn&1)&&(pc&3))||!p10_read_allowed(P10_IMAGE,pc,32)||!read(context,P10_IMAGE,pc,e->code,32))return 0;
  e->present=1;
 }
 return 1;
}
static int p10_owner_valid(const struct p10_owner *o){
 if(o->magic!=P10_OWNER_MAGIC||o->version!=1||o->bytes!=sizeof *o||o->count!=2||o->table_pa!=P10_OBSERVER_TABLE||o->object_bytes!=32||o->code_bytes!=32||o->reserved)return 0;
 for(unsigned i=0;i<2;i++){
  const struct p10_owner_entry *e=o->entry+i;if(e->selector!=7+i||e->present>1||e->object_pa!=o->table[i?2:3])return 0;
  if(!e->object_pa){struct p10_owner_entry empty={.selector=7+i};if(memcmp(e,&empty,sizeof empty))return 0;continue;}
  if(!e->present||!p10_read_allowed(P10_OBJECT,e->object_pa,32)||e->vtable_pa!=e->object[0]||(e->vtable_pa&3)||!p10_read_allowed(P10_IMAGE,e->vtable_pa,16)||e->notify_fn!=e->vtable[2]||!e->notify_fn||(!(e->notify_fn&1)&&(e->notify_fn&3))||!p10_read_allowed(P10_IMAGE,e->notify_fn&~1u,32))return 0;
 }
 return 1;
}
static int p10_owner_same_identity(const struct p10_owner *a,const struct p10_owner *b){
 if(!p10_owner_valid(a)||!p10_owner_valid(b))return 0;
 for(unsigned i=0;i<2;i++){
  const struct p10_owner_entry *x=a->entry+i,*y=b->entry+i;
  if(x->present!=y->present||x->object_pa!=y->object_pa||x->vtable_pa!=y->vtable_pa||x->notify_fn!=y->notify_fn||memcmp(x->vtable,y->vtable,sizeof x->vtable)||memcmp(x->code,y->code,sizeof x->code))return 0;
 }
 return 1;
}
static int p10_sample_source_pa(const struct h3dl_sample *s,uint32_t *pa){
 unsigned bank=(s->lut[1]>>4)&3;
 if(!s->clock_on||bank>1||!((s->lut[1]>>(bank?12:8))&1)||!p10_source_pa_valid(s->lut[3]))return 0;
 *pa=s->lut[3];return 1;
}
static int p10_capture_source_pa(const struct h3dl_capture *c,uint32_t *pa){
 if(!h3dl_capture_valid(c)||!p10_sample_source_pa(c->sample,pa))return 0;
 const struct h3dl_sample *s=c->sample;
 for(unsigned i=0;i<H3DL_SAMPLES;i++)if(!c->sample[i].clock_on||memcmp(s->lut,c->sample[i].lut,sizeof s->lut))return 0;
 return 1;
}
#endif
