/* SPDX-License-Identifier: AGPL-3.0-only. Source-bound LDC curves.
 * Recorded tables remain reproducible; estimated curves carry provenance. */
#ifndef NXL_LENS_PROFILE_H
#define NXL_LENS_PROFILE_H
#include "native_jpeg.h"
#define LP_MAGIC 0x3143504cu
struct lp_key {uint32_t schema,lens,sensor[8],focal35x10;char firmware[64],serial[64];uint32_t correction[54];};
_Static_assert(sizeof(struct lp_key)==388,"normalized observed lens key");
struct lp_config {
 uint32_t magic,schema,bytes,profile_index;
 char source_sha256[68],profile_key[68],table_sha256[68];
 struct lp_key key;
 unsigned char source_packet[272];
 uint32_t reserved[35],checksum;
};
_Static_assert(sizeof(struct lp_config)==1024,"LDC source binding wire extent");
struct lp_catalog_item {struct lp_key key;unsigned char packet[272];char profile_key[65],table_sha[65];};
#include "lens_profile_catalog.h"
#include "lens_curve_select.h"
struct lp_entry {unsigned type,count;uint32_t at;};
static int lp_string(struct nj_reader*r,const struct lp_entry*e,uint32_t lo,uint32_t hi,char out[64]){
 unsigned char b[256];if(e->type!=2||!e->count||e->count>sizeof b||e->at<lo||e->at>hi||e->count>hi-e->at||!nj_read(r,e->at,b,e->count))return 0;
 unsigned n=0;while(n<e->count&&b[n]){if(n>=63||b[n]<32||b[n]>126)return 0;n++;}if(n==e->count)return 0;memset(out,0,64);memcpy(out,b,n);return 1;
}
static int lp_array(struct nj_reader*r,const struct lp_entry*e,unsigned type,unsigned count,int le,uint32_t lo,uint32_t hi,uint32_t*out){
 unsigned char b[128];unsigned unit=type==3?2u:4u;
 if(e->type!=type||e->count!=count||count*unit>sizeof b||e->at<lo||e->at>hi||count*unit>hi-e->at||!nj_read(r,e->at,b,count*unit))return 0;
 for(unsigned i=0;i<count;i++)out[i]=unit==2?nj_u16(b+i*unit,le):nj_u32(b+i*unit,le);return 1;
}
static int lp_maker(struct nj_reader*r,uint32_t root,int le,uint32_t*maker,uint32_t*end){
 unsigned char h[2],e[12];uint32_t exif=0;
 for(unsigned level=0;level<2;level++){
  uint32_t at=level?exif:root;if(!nj_read(r,at,h,2))return 0;unsigned n=nj_u16(h,le);if(n>512)return 0;
  unsigned found=0;for(unsigned i=0;i<n;i++){
   if(!nj_read(r,(uint64_t)at+2+i*12,e,12))return 0;unsigned tag=nj_u16(e,le);
   if(tag!=(level?0x927c:0x8769))continue;if(found++)return 0;
   if(!level){if(nj_u16(e+2,le)!=4||nj_u32(e+4,le)!=1)return 0;exif=nj_u32(e+8,le);}
   else{if(nj_u16(e+2,le)!=7)return 0;*maker=nj_u32(e+8,le);uint32_t size=nj_u32(e+4,le);if(size<6||(uint64_t)*maker+size>r->size)return 0;*end=*maker+size;}
  }if(found!=1)return 0;
 }return 1;
}
static int lp_from_srw(struct nj_reader*r,struct lp_config*out,const char**reason){
 if(reason)*reason="SRW_LDC_METADATA_INVALID";unsigned char h[8],table[6144];
 if(!nj_read(r,0,h,8)||!((h[0]=='I'&&h[1]=='I')||(h[0]=='M'&&h[1]=='M')))return 0;int le=h[0]=='I';if(nj_u16(h+2,le)!=42)return 0;
 uint32_t base=0,end=0;if(!lp_maker(r,nj_u32(h+4,le),le,&base,&end)||!nj_read(r,base,h,2))return 0;
 unsigned count=nj_u16(h,le);if(count>512||(uint64_t)base+2+count*12+4>end||!nj_read(r,(uint64_t)base+2,table,count*12))return 0;
 const uint16_t wanted[12]={0xa003,0xa004,0xa005,0xa010,0xa01a,0xa020,0xa050,0xa051,0xa055,0xa056,0xa057,0xa011};
 struct lp_entry entries[12];memset(entries,0,sizeof entries);unsigned mask=0;
 for(unsigned i=0;i<count;i++){
  const unsigned char*e=table+i*12;unsigned tag=nj_u16(e,le);
  for(unsigned j=0;j<12;j++)if(tag==wanted[j]){
   if(mask&(1u<<j))return 0;mask|=1u<<j;struct lp_entry*x=&entries[j];x->type=nj_u16(e+2,le);x->count=nj_u32(e+4,le);
   unsigned unit=x->type==3?2u:x->type==4||x->type==9?4u:1u;
   uint64_t at=(uint64_t)base+nj_u32(e+8,le);if((uint64_t)unit*x->count<=4)at=(uint64_t)base+2+i*12+8;if(at>0xffffffffu)return 0;x->at=(uint32_t)at;
  }
 }
 if((mask&0x7ffu)!=0x7ffu)return 0;
 memset(out,0,sizeof *out);struct lp_key*k=&out->key;k->schema=1;uint32_t key[11];
 if(!lp_array(r,entries,3,1,le,base,end,&k->lens)||!lp_string(r,entries+1,base,end,k->firmware)||!lp_string(r,entries+2,base,end,k->serial)||
 !lp_array(r,entries+3,4,8,le,base,end,k->sensor)||!lp_array(r,entries+4,4,1,le,base,end,&k->focal35x10)||!lp_array(r,entries+5,4,11,le,base,end,key))return 0;
 unsigned dest=0;const unsigned n[5]={8,22,8,8,8},salt[5]={0,0,8,5,2};
 for(unsigned j=0;j<5;j++){
  uint32_t v[22];if(!lp_array(r,entries+6+j,j==1?3u:9u,n[j],le,base,end,v))return 0;
  if(j==1){if(v[0]!=7)return 0;for(unsigned group=0;group<3;group++)for(unsigned i=0;i<7;i++){const unsigned s[3]={0,7,3};unsigned at=1+group*7+i;v[at]=(v[at]-key[(s[group]+i)%11])&65535u;}}
  else for(unsigned i=0;i<n[j];i++)v[i]+=key[(salt[j]+i)%11];
  for(unsigned i=0;i<n[j];i++)k->correction[dest++]=v[i];
 }
 if(dest!=54)return 0;
 return lc_resolve(k,out,reason);
}

static void lp_seal(struct lp_config*c,const char*source){memset(c->source_sha256,0,68);memcpy(c->source_sha256,source,64);c->checksum=nj_hash(c,offsetof(struct lp_config,checksum));}
static int lp_valid(const struct lp_config*c,const char*source){
 if(c->magic!=LP_MAGIC||(c->schema!=1&&c->schema!=2)||c->bytes!=sizeof *c||
 c->checksum!=nj_hash(c,offsetof(struct lp_config,checksum))||memcmp(c->source_sha256,source,64))return 0;
 for(unsigned i=64;i<68;i++)if(c->source_sha256[i]||c->profile_key[i]||c->table_sha256[i])return 0;
 if(c->schema==1){
  if(c->profile_index>=sizeof lp_catalog/sizeof *lp_catalog)return 0;
  const struct lp_catalog_item*p=&lp_catalog[c->profile_index];
  if(memcmp(&c->key,&p->key,sizeof c->key)||memcmp(c->source_packet,p->packet,272)||memcmp(c->profile_key,p->profile_key,65)||memcmp(c->table_sha256,p->table_sha,65))return 0;
  for(unsigned i=0;i<35;i++)if(c->reserved[i])return 0;return 1;
 }
 struct lp_config expected;
 if(!lc_resolve(&c->key,&expected,NULL)||c->profile_index!=expected.profile_index||
 memcmp(c->source_packet,expected.source_packet,272)||memcmp(c->profile_key,expected.profile_key,68)||
 memcmp(c->table_sha256,expected.table_sha256,68)||memcmp(c->reserved,expected.reserved,sizeof c->reserved))return 0;
 return 1;
}

static void lp_pack(const struct lp_config*c,uint32_t out[72]){
 const unsigned char*p=c->source_packet;memset(out,0,288);out[1]=nj_u32(p+268,1);out[2]=nj_u32(p+260,1);out[3]=nj_u32(p+264,1);
 out[4]=out[5]=out[6]=out[7]=128;memcpy(out+8,p+4,256);
}
#endif
