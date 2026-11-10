/* SPDX-License-Identifier: AGPL-3.0-only. One physical allocation, one live owner. */
#ifndef NXL_SHARED_WORKSPACE_H
#define NXL_SHARED_WORKSPACE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef NXL_SMALL_MODEL
#define SW_BYTES (512u*1024u)
#define SW_FRONT_ACTIVE (256u*1024u)
#define SW_W 64u
#define SW_H 32u
#define SW_FRONT_OUT 0x20000u
#define SW_LDC_OUT 0x38000u
#else
#define SW_BYTES (192u*1024u*1024u)
#define SW_FRONT_ACTIVE (128u*1024u*1024u)
#define SW_W 6480u
#define SW_H 4320u
#define SW_FRONT_OUT (64u*1024u*1024u)
#define SW_LDC_OUT (128u*1024u*1024u)
#endif
#define SW_CONTROL 65536u
#define SW_INPUT SW_FRONT_OUT
#define SW_LUT_OUT SW_CONTROL
#define SW_JPEG_OUT SW_FRONT_OUT
#define SW_TABLE (SW_BYTES-0x20000u)
#define SW_LEASE (SW_BYTES-16384u)
#define SW_FRAME (SW_W*SW_H*2u)
#define SW_FRONT_STRIDE (SW_W+16u)
#define SW_FRONT_UV (((SW_FRONT_STRIDE*SW_H)+65535u)&~65535u)
#define SW_MAGIC 0x3157534eu
enum sw_phase {SW_FRONT_READY=1,SW_BACKEND_ACTIVE,SW_BACKEND_DONE,SW_QUARANTINED,SW_RELEASED};
struct sw_lease {
 uint32_t magic,version,bytes,phase,capacity,front_active,input_offset,lut_offset,jpeg_offset,table_offset,lease_offset;
 uint32_t pa,va,nonce,owner_pid,index;
 uint64_t owner_start_ticks;
 uint32_t metadata[4];
 char prefix_sha256[68],lut_sha256[68],source_sha256[68];
 uint32_t front_return_barrier,frame_callbacks_retired,controller_slot_restored;
 uint32_t reserved[51],checksum;
};
_Static_assert(sizeof(struct sw_lease)==512,"fixed lease ABI");
_Static_assert(SW_LUT_OUT+SW_FRAME+16384u<SW_INPUT,"disjoint NV16 source and LUT destination");
_Static_assert(SW_INPUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H+16384u<SW_TABLE,"LUT frame within active area");
_Static_assert(SW_INPUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H+16384u<SW_LDC_OUT,"Bayer and LDC frames disjoint");
_Static_assert(SW_LDC_OUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H+16384u<SW_TABLE,"LDC frame before tables");
_Static_assert(SW_TABLE+0xf000u+19712u<SW_LEASE,"table slots before lease guard");
static uint32_t sw_checksum(const void *data,size_t n){const unsigned char *p=data;uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void sw_seal(struct sw_lease *s){s->checksum=sw_checksum(s,offsetof(struct sw_lease,checksum));}
static int sw_hash_valid(const char s[68]){for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return !s[64]&&!s[65]&&!s[66]&&!s[67];}
static int sw_valid_capacity(const struct sw_lease *s,unsigned capacity){
 if(s->magic!=SW_MAGIC||s->version!=1||s->bytes!=sizeof *s||s->phase<SW_FRONT_READY||s->phase>SW_RELEASED||
    s->capacity!=capacity||s->front_active!=SW_FRONT_ACTIVE||s->input_offset!=SW_INPUT||s->lut_offset!=SW_LUT_OUT||
    s->jpeg_offset!=SW_JPEG_OUT||s->table_offset!=capacity-0x20000u||s->lease_offset!=capacity-16384u||!s->nonce||s->owner_pid<=1||!s->owner_start_ticks||
    s->pa<0x94000000u||(s->pa&16383u)||(uint64_t)s->pa+capacity>0xbb6cc000ULL||!s->va||(s->va&16383u)||
    (uint64_t)s->va+capacity>0x100000000ULL||s->index!=(s->pa-0x94000000u)/16384u||
    (s->metadata[0]&255u)!=35||s->metadata[1]!=capacity/16384u||
    !sw_hash_valid(s->prefix_sha256)||!sw_hash_valid(s->lut_sha256)||!sw_hash_valid(s->source_sha256)||
    s->front_return_barrier!=1||s->frame_callbacks_retired!=1||s->controller_slot_restored!=1||
    s->checksum!=sw_checksum(s,offsetof(struct sw_lease,checksum)))return 0;
 for(unsigned i=0;i<51;i++)if(s->reserved[i])return 0;return 1;
}
static int sw_valid(const struct sw_lease*s){return sw_valid_capacity(s,SW_BYTES);}
/* Old completed160MiB jobs remain checkable after a192MiB software upgrade.
 * Only the receipt reader calls this. Runtime borrowing still requires SW_BYTES. */
static int sw_receipt_valid(const struct sw_lease*s){return (s->capacity==167772160u||s->capacity==SW_BYTES)&&sw_valid_capacity(s,s->capacity);}
static int sw_receipt_same(const struct sw_lease*a,const struct sw_lease*b){if(!sw_receipt_valid(a)||!sw_receipt_valid(b))return 0;struct sw_lease x=*a,y=*b;x.phase=y.phase=0;x.checksum=y.checksum=0;return !memcmp(&x,&y,sizeof x);}
static int sw_same_allocation(const struct sw_lease*a,const struct sw_lease*b){
 if(!sw_valid(a)||!sw_valid(b))return 0;struct sw_lease x=*a,y=*b;x.phase=y.phase=0;x.checksum=y.checksum=0;return !memcmp(&x,&y,sizeof x);
}
static int sw_transition(struct sw_lease*s,unsigned expected,unsigned next){
 if(!sw_valid(s)||s->phase!=expected||!((expected==SW_FRONT_READY&&next==SW_BACKEND_ACTIVE)||
    (expected==SW_BACKEND_ACTIVE&&(next==SW_BACKEND_DONE||next==SW_QUARANTINED))||
    ((expected==SW_FRONT_READY||expected==SW_BACKEND_DONE||expected==SW_QUARANTINED)&&next==SW_RELEASED)))return 0;
 s->phase=next;sw_seal(s);return 1;
}
static int sw_allocation_in_table(const struct sw_lease*s,const uint32_t*t,unsigned blocks,unsigned free_count){
 if(!sw_valid(s)||!t||blocks!=45056u)return 0;unsigned free_seen=0,owned=0;
 for(unsigned i=0;i<blocks;){unsigned kind=t[4*i]&255u,span=t[4*i+1];
  if(!kind){if(span)return 0;free_seen++;i++;continue;}
  if(!span||span>blocks-i)return 0;
  if(i==s->index){if(memcmp(t+4*i,s->metadata,16))return 0;owned=1;}
  else if(i<s->index+SW_BYTES/16384u&&i+span>s->index)return 0;
  for(unsigned j=1;j<span;j++){unsigned k=t[4*(i+j)]&255u,n=t[4*(i+j)+1];if(k?(k!=kind||n!=span-j):n!=0)return 0;}
  i+=span;
 }
 return owned&&free_seen==free_count;
}
enum sw_return_code {SW_RETURN_OK,SW_RETURN_INPUT_INVALID,SW_RETURN_FREE_HAS_SPAN,SW_RETURN_BAD_SPAN,SW_RETURN_LEASE_OVERLAP,SW_RETURN_BAD_INTERIOR,SW_RETURN_FREE_COUNT_MISMATCH};
struct sw_return_observation {uint32_t code,index,kind,span,free_seen,free_reported,lease_zero_blocks,head_matches_old,head_words[4];};
static int sw_return_observe(const struct sw_lease*s,const uint32_t*t,unsigned blocks,unsigned free_count,struct sw_return_observation*out){
 struct sw_return_observation local={0};struct sw_return_observation*r=out?out:&local;r->code=SW_RETURN_INPUT_INVALID;r->index=UINT32_MAX;r->free_seen=0;r->free_reported=free_count;r->lease_zero_blocks=r->head_matches_old=0;memset(r->head_words,0,sizeof r->head_words);r->kind=r->span=0;
 if(!sw_valid(s)||!t||blocks!=45056u)return 0;
 memcpy(r->head_words,t+s->index*4,16);r->head_matches_old=!memcmp(r->head_words,s->metadata,16);
 for(unsigned i=s->index;i<s->index+SW_BYTES/16384u;i++)if(!(t[4*i]&255u)&&!t[4*i+1])r->lease_zero_blocks++;
 unsigned free_seen=0;
 for(unsigned i=0;i<blocks;){unsigned kind=t[4*i]&255u,span=t[4*i+1];
  r->index=i;r->kind=kind;r->span=span;r->free_seen=free_seen;
  if(!kind){if(span){r->code=SW_RETURN_FREE_HAS_SPAN;return 0;}free_seen++;i++;continue;}
  if(!span||span>blocks-i){r->code=SW_RETURN_BAD_SPAN;return 0;}
  if(i<s->index+SW_BYTES/16384u&&i+span>s->index){r->code=SW_RETURN_LEASE_OVERLAP;return 0;}
  for(unsigned j=1;j<span;j++){unsigned k=t[4*(i+j)]&255u,n=t[4*(i+j)+1];if(k?(k!=kind||n!=span-j):n!=0){r->code=SW_RETURN_BAD_INTERIOR;r->index=i+j;r->kind=k;r->span=n;return 0;}}
  i+=span;
 }
 r->free_seen=free_seen;r->index=UINT32_MAX;r->kind=r->span=0;r->code=free_seen==free_count?SW_RETURN_OK:SW_RETURN_FREE_COUNT_MISMATCH;return free_seen==free_count;
}
static int sw_range_returned(const struct sw_lease*s,const uint32_t*t,unsigned blocks,unsigned free_count){
 return sw_return_observe(s,t,blocks,free_count,NULL);
}
#endif
