/* SPDX-License-Identifier: AGPL-3.0-only. Bounded SRW full-JPEG headers only.
 * No entropy decoding, quality guessing, thumbnail fallback or input writes. */
#ifndef NXL_NATIVE_JPEG_H
#define NXL_NATIVE_JPEG_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define NJ_MAGIC 0x31434a4eu
struct nj_config {
 uint32_t magic,schema,bytes,sampling;
 char source_sha256[68];
 uint32_t jpeg_offset,jpeg_bytes;
 unsigned char quant[2][64]; /* Natural matrix order, not serialized zigzag. */
 uint32_t reserved[8],checksum;
};
_Static_assert(sizeof(struct nj_config)==256,"JPEG configuration wire extent");
struct nj_reader {void *ctx;uint64_t size;int (*read)(void*,uint64_t,void*,unsigned);unsigned read_bytes;};
static uint32_t nj_hash(const void*p,unsigned n){const unsigned char*b=p;uint32_t h=2166136261u;while(n--)h=(h^*b++)*16777619u;return h;}
static int nj_read(struct nj_reader*r,uint64_t at,void*p,unsigned n){if(at>r->size||n>r->size-at||n>65536u||r->read_bytes>262144u-n)return 0;r->read_bytes+=n;return r->read(r->ctx,at,p,n);}
static uint16_t nj_u16(const unsigned char*p,int le){return le?(uint16_t)(p[0]|p[1]<<8):(uint16_t)(p[0]<<8|p[1]);}
static uint32_t nj_u32(const unsigned char*p,int le){return le?(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24:(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int nj_values(struct nj_reader*r,const unsigned char*e,int le,uint32_t*out,unsigned cap){
 unsigned type=nj_u16(e+2,le),n=nj_u32(e+4,le),unit=type==3?2u:type==4?4u:0u;
 if(!unit||!n||n>cap||n>16)return 0;unsigned char b[64];const unsigned char*p=e+8;
 if(n*unit>4){if(!nj_read(r,nj_u32(e+8,le),b,n*unit))return 0;p=b;}
 for(unsigned i=0;i<n;i++)out[i]=unit==2?nj_u16(p+i*unit,le):nj_u32(p+i*unit,le);return (int)n;
}
static const unsigned char nj_zigzag[64]={0,1,8,16,9,2,3,10,17,24,32,25,18,11,4,5,12,19,26,33,40,48,41,34,27,20,13,6,7,14,21,28,35,42,49,56,57,50,43,36,29,22,15,23,30,37,44,51,58,59,52,45,38,31,39,46,53,60,61,54,47,55,62,63};
/* Returns 1 full frame, 2 another valid dimension (thumbnail), 0 malformed. */
static int nj_jpeg(struct nj_reader*r,uint32_t begin,uint32_t bytes,struct nj_config*out){
 unsigned char b[260],tables[4][64];unsigned have=0,sof=0,w=0,h=0,sampling[3]={0},qt[3]={0};
 if(bytes<32||(uint64_t)begin+bytes>r->size||!nj_read(r,begin,b,2)||b[0]!=255||b[1]!=216||!nj_read(r,(uint64_t)begin+bytes-2,b,2)||b[0]!=255||b[1]!=217)return 0;
 uint64_t at=(uint64_t)begin+2,end=(uint64_t)begin+bytes;
 for(unsigned count=0;count<256&&at+4<=end;count++){
  if(at-begin>262144u||!nj_read(r,at,b,4)||b[0]!=255||b[1]==0||b[1]==255)return 0;
  unsigned marker=b[1],size=nj_u16(b+2,0);if(size<2||size>end-at-2)return 0;
  if(marker==0xc0){
   if(sof++||size!=17||!nj_read(r,at+4,b,15)||b[0]!=8||b[5]!=3)return 0;
   h=nj_u16(b+1,0);w=nj_u16(b+3,0);
   for(unsigned i=0;i<3;i++){if(b[6+3*i]!=i+1)return 0;sampling[i]=b[7+3*i];qt[i]=b[8+3*i];}
  }else if(marker==0xdb){
   unsigned left=size-2;uint64_t q=at+4;
   while(left){
    if(left<65||!nj_read(r,q,b,65)||(b[0]>>4)||((b[0]&15)>3))return 0;
    unsigned id=b[0]&15;if(have&(1u<<id))return 0;have|=1u<<id;
    for(unsigned i=0;i<64;i++){if(!b[i+1])return 0;tables[id][nj_zigzag[i]]=b[i+1];}
    q+=65;left-=65;
   }
  }else if(marker==0xda){
   if(sof!=1||size!=12||!nj_read(r,at+4,b,10)||b[0]!=3||b[1]!=1||b[3]!=2||b[5]!=3||b[7]!=0||b[8]!=63||b[9]!=0)return 0;
   if(w!=6480||h!=4320)return 2;
   if(sampling[0]!=0x21||sampling[1]!=0x11||sampling[2]!=0x11||qt[0]>3||qt[1]>3||qt[2]!=qt[1]||!(have&(1u<<qt[0]))||!(have&(1u<<qt[1])))return 0;
   memset(out,0,sizeof *out);out->magic=NJ_MAGIC;out->schema=1;out->bytes=sizeof *out;out->sampling=422;out->jpeg_offset=begin;out->jpeg_bytes=bytes;
   memcpy(out->quant[0],tables[qt[0]],64);memcpy(out->quant[1],tables[qt[1]],64);return 1;
  }else if(marker==0xd8||marker==0xd9||(marker>=0xc0&&marker<=0xcf&&marker!=0xc4))return 0;
  at+=size+2u;
 }return 0;
}
static int nj_from_srw(struct nj_reader*r,struct nj_config*out,const char**reason){
 unsigned char head[8],table[6148];uint32_t queue[64],seen[64];unsigned used=0,next=0,visited=0,found=0;
 if(reason)*reason="SRW_FULL_JPEG_HEADER_INVALID";
 if(!nj_read(r,0,head,8)||!((head[0]=='I'&&head[1]=='I')||(head[0]=='M'&&head[1]=='M')))return 0;
 int le=head[0]=='I';if(nj_u16(head+2,le)!=42)return 0;queue[used++]=nj_u32(head+4,le);
 while(next<used){
  uint32_t at=queue[next++];if(!at)continue;unsigned duplicate=0;for(unsigned i=0;i<visited;i++)duplicate|=seen[i]==at;if(duplicate)continue;
  if(visited>=64||!nj_read(r,at,head,2))return 0;seen[visited++]=at;unsigned n=nj_u16(head,le);if(n>512||!nj_read(r,(uint64_t)at+2,table,n*12+4))return 0;
  uint32_t offset=0,bytes=0,children[16];unsigned children_count=0;
  for(unsigned i=0;i<n;i++){
   const unsigned char*e=table+i*12;unsigned tag=nj_u16(e,le);uint32_t v[16];int count;
   if(tag!=0x201&&tag!=0x202&&tag!=0x14a)continue;
   if((count=nj_values(r,e,le,v,16))<=0)return 0;
   if(tag==0x14a){if(children_count)return 0;children_count=(unsigned)count;memcpy(children,v,(size_t)count*4);}
   else if(count!=1)return 0;else if(tag==0x201){if(offset)return 0;offset=v[0];}else{if(bytes)return 0;bytes=v[0];}
  }
  uint32_t following=nj_u32(table+n*12,le);
  for(unsigned i=0;i<children_count+(following!=0);i++){if(used>=64)return 0;queue[used++]=i<children_count?children[i]:following;}
  if(offset||bytes){struct nj_config candidate;int rc=offset&&bytes?nj_jpeg(r,offset,bytes,&candidate):0;if(!rc)return 0;
   if(rc==1){if(found++){if(reason)*reason="SRW_MULTIPLE_FULL_JPEG_HEADERS";return 0;}*out=candidate;}
  }
 }
 if(found!=1){if(reason)*reason="SRW_FULL_JPEG_NOT_FOUND";return 0;}return 1;
}
static void nj_seal(struct nj_config*c,const char*source){memset(c->source_sha256,0,sizeof c->source_sha256);memcpy(c->source_sha256,source,64);c->checksum=nj_hash(c,offsetof(struct nj_config,checksum));}
static int nj_valid(const struct nj_config*c,const char*source){
 if(c->magic!=NJ_MAGIC||c->schema!=1||c->bytes!=sizeof *c||c->sampling!=422||!c->jpeg_offset||c->jpeg_bytes<32||
 c->checksum!=nj_hash(c,offsetof(struct nj_config,checksum))||memcmp(c->source_sha256,source,64))return 0;
 for(unsigned i=0;i<64;i++)if(!((c->source_sha256[i]>='0'&&c->source_sha256[i]<='9')||(c->source_sha256[i]>='a'&&c->source_sha256[i]<='f')))return 0;
 for(unsigned i=64;i<68;i++)if(c->source_sha256[i])return 0;for(unsigned i=0;i<8;i++)if(c->reserved[i])return 0;
 for(unsigned i=0;i<128;i++)if(!((const unsigned char*)c->quant)[i])return 0;return 1;
}
#endif
