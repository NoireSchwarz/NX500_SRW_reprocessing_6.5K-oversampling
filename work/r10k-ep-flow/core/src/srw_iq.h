/* SPDX-License-Identifier: AGPL-3.0-only. Bounded native NX500 TIFF metadata.
 * Input is read-only. All MakerNote value pointers are relative to its own IFD.
 * Gains use A021 as recorded, not A021-black (black correction stays separate).
 */
#ifndef P41_SRW_IQ_H
#define P41_SRW_IQ_H
#include <stdio.h>
#include <string.h>
#include "wb_contract.h"
struct p41_entry {uint32_t type,count,at;};
struct p41_reader {FILE *f;uint64_t size;uint32_t reads;int le;};
static uint32_t p41_u16(const unsigned char *p,int le){return le?p[0]|(uint32_t)p[1]<<8:(uint32_t)p[0]<<8|p[1];}
static uint32_t p41_u32(const unsigned char *p,int le){return le?p41_u16(p,1)|(p41_u16(p+2,1)<<16):(p41_u16(p,0)<<16)|p41_u16(p+2,0);}
static int p41_read(struct p41_reader *r,uint64_t at,void *out,unsigned n,uint64_t lo,uint64_t hi){
 if(at<lo||at>hi||n>hi-at||hi>r->size||fseeko(r->f,(off_t)at,SEEK_SET)||fread(out,1,n,r->f)!=n)return 0;r->reads+=n;return 1;
}
static int p41_find(struct p41_reader *r,uint32_t at,uint32_t base,uint64_t lo,uint64_t hi,uint32_t tag,struct p41_entry *out){
 unsigned char count[2],e[12];uint16_t seen[512];
 if(!p41_read(r,at,count,2,lo,hi))return 0;unsigned n=p41_u16(count,r->le);if(n>512||(uint64_t)at+2+12u*n+4>hi)return 0;
 int found=0;for(unsigned i=0;i<n;i++){
  if(!p41_read(r,(uint64_t)at+2+12u*i,e,12,lo,hi))return 0;uint32_t t=p41_u16(e,r->le);
  for(unsigned j=0;j<i;j++)if(seen[j]==t)return 0;seen[i]=(uint16_t)t;
  if(t==tag){out->type=p41_u16(e+2,r->le);out->count=p41_u32(e+4,r->le);unsigned width=out->type==3?2:(out->type==4||out->type==9)?4:1;
   uint64_t off=(uint64_t)base+p41_u32(e+8,r->le);if((uint64_t)width*out->count<=4)off=(uint64_t)at+2+12u*i+8;if(off>0xffffffffu)return 0;out->at=(uint32_t)off;found=1;}
 }return found;
}
static int p41_values(struct p41_reader *r,const struct p41_entry *e,unsigned type,unsigned n,uint64_t lo,uint64_t hi,uint32_t *v){
 unsigned char b[44];if(e->type!=type||e->count!=n||n>11||!p41_read(r,e->at,b,n*4,lo,hi))return 0;
 for(unsigned i=0;i<n;i++)v[i]=p41_u32(b+4*i,r->le);return 1;
}
static int p41_srw_info_ex(FILE *f,uint32_t v[256],unsigned char *c0_out){
 memset(v,0,1024);struct p41_reader r={f,0,0,0};unsigned char h[8],name[64];struct p41_entry e;uint32_t tmp[11],key[11];
 if(fseeko(f,0,SEEK_END))return 0;off_t size=ftello(f);if(size<8)return 0;r.size=(uint64_t)size;
 if(!p41_read(&r,0,h,8,0,r.size)||!((h[0]=='I'&&h[1]=='I')||(h[0]=='M'&&h[1]=='M')))return 0;r.le=h[0]=='I';if(p41_u16(h+2,r.le)!=42)return 0;
 uint32_t root=p41_u32(h+4,r.le);
 for(unsigned i=0;i<2;i++){
  if(!p41_find(&r,root,0,0,r.size,i?0x110:0x10f,&e)||e.type!=2||!e.count||e.count>sizeof name||!p41_read(&r,e.at,name,e.count,0,r.size)||!memchr(name,0,e.count))return 0;
  if(strcmp((char*)name,i?"NX500":"SAMSUNG"))return 0;
 }
 if(!p41_find(&r,root,0,0,r.size,0x8769,&e)||!p41_values(&r,&e,4,1,0,r.size,tmp))return 0;uint32_t exif=tmp[0];
 if(!p41_find(&r,exif,0,0,r.size,0x927c,&e)||e.type!=7||e.count<6||(uint64_t)e.at+e.count>r.size)return 0;
 uint32_t base=e.at;uint64_t end=(uint64_t)base+e.count;
 if(!p41_find(&r,base,base,base,end,0xa020,&e)||!p41_values(&r,&e,4,11,base,end,key))return 0;
 if(!p41_find(&r,base,base,base,end,0xa014,&e)||!p41_values(&r,&e,4,1,base,end,tmp))return 0;v[4]=tmp[0];v[5]=base;
 const unsigned tags[5]={0xa021,0xa028,0xa030,0xa031,0xa032};
 for(unsigned k=0;k<5;k++){
  unsigned n=k<2?4:9,type=k?9:4,dest=k<2?12+4*k:20+9*(k-2);
  if(!p41_find(&r,base,base,base,end,tags[k],&e)||!p41_values(&r,&e,type,n,base,end,tmp))return 0;
  for(unsigned i=0;i<n;i++)v[dest+i]=k<2?tmp[i]-key[i]:tmp[i]+key[i];
 }
 if(!v[13])return 0;for(unsigned i=0;i<4;i++){uint64_t q=(uint64_t)v[12+i]*256/v[13];if(q>2047||q<64)return 0;v[8+i]=(uint32_t)q;}
 /* C0 is explicitly little-endian, independent of the outer TIFF byte order.
  * Its exact2048B extent remains inside the containing MakerNote. */
 unsigned char c0[P49_WB_C0_BYTES];
 if(!p41_find(&r,base,base,base,end,0x00c0,&e)||e.type!=7||e.count!=P49_WB_C0_BYTES||
    !p41_read(&r,e.at,c0,sizeof c0,base,end))return 0;
 if(c0_out)memcpy(c0_out,c0,sizeof c0);
 uint32_t digest=2166136261u;for(unsigned i=0;i<sizeof c0;i++)digest=(digest^c0[i])*16777619u;
 v[47]=digest;v[51]=p41_u32(c0+520,1);v[52]=p41_u32(c0+56,1);v[53]=e.at;
 v[54]=p41_u32(c0,1);v[55]=p41_u32(c0+4,1);v[56]=p41_u32(c0+8,1);
 v[57]=p49_wb_selector(v[51]);v[58]=p49_wb_flag(v[51]);v[59]=0;v[60]=1;v[61]=1;v[62]=p50_black_profile(v+16,v[51]);
 v[0]=P41_MAGIC;v[1]=P49_WB_REQUEST_SCHEMA;v[3]=1;v[6]=r.reads;v[7]=r.le;v[63]=p41_checksum(v,63);return p41_request_valid(v,0);
}
static int p41_srw_info(FILE *f,uint32_t v[256]){return p41_srw_info_ex(f,v,NULL);}
#endif
