#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "../../common/lens_profile.h"
#include "../../common/pixel_sample.h"
static unsigned char data[4096];static unsigned bytes,reads;
static int load(void*u,uint64_t at,void*p,unsigned n){(void)u;assert(at<=bytes&&n<=bytes-at);memcpy(p,data+(unsigned)at,n);reads+=n;return 1;}
static void u16(unsigned char*p,unsigned x){p[0]=(unsigned char)x;p[1]=(unsigned char)(x>>8);}
static void u32(unsigned char*p,unsigned x){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(8*i));}
static unsigned jpeg(unsigned at){unsigned start=at;data[at++]=255;data[at++]=216;data[at++]=255;data[at++]=219;data[at++]=0;data[at++]=132;
 for(unsigned q=0;q<2;q++){data[at++]=(unsigned char)q;for(unsigned i=0;i<64;i++)data[at++]=(unsigned char)(i+1+q);}
 const unsigned char tail[]={255,192,0,17,8,16,224,25,80,3,1,33,0,2,17,1,3,17,1,255,218,0,12,3,1,0,2,17,3,17,0,63,0,0,255,217};memcpy(data+at,tail,sizeof tail);return at+sizeof tail-start;
}
int main(void){unsigned n=jpeg(64);bytes=64+n;struct nj_reader r={NULL,bytes,load,0};struct nj_config j;const char*why;
 assert(nj_jpeg(&r,64,n,&j)==1&&reads<4096);for(unsigned i=0;i<64;i++)assert(j.quant[0][nj_zigzag[i]]==i+1);
 char source[68]={0};memset(source,'c',64);nj_seal(&j,source);assert(nj_valid(&j,source));j.quant[0][0]^=1;assert(!nj_valid(&j,source));
 unsigned char saved[4096];memcpy(saved,data,sizeof data);unsigned at=64+136;
 const unsigned pos[]={64,64+bytes-65,64+6,at+1,at+4,at+11,at+13,at+19};
 for(unsigned i=0;i<sizeof pos/sizeof *pos;i++){memcpy(data,saved,sizeof data);data[pos[i]]^=127;r.read_bytes=0;assert(!nj_jpeg(&r,64,n,&j));}
 memcpy(data,saved,sizeof data);data[at+7]=0;data[at+8]=32;r.read_bytes=0;assert(nj_jpeg(&r,64,n,&j)==2);memcpy(data,saved,sizeof data);
 r.read_bytes=262140;assert(!nj_jpeg(&r,64,n,&j));r.read_bytes=0;assert(!nj_jpeg(&r,0xfffffff0u,n,&j));
 /* Classic TIFF header, two full-JPEG references in distinct IFDs must fail. */
 memset(data,0,64);data[0]=data[1]='I';u16(data+2,42);u32(data+4,8);u16(data+8,2);
 for(unsigned i=0;i<2;i++){unsigned char*e=data+10+i*12;u16(e,0x201+i);u16(e+2,4);u32(e+4,1);u32(e+8,i?n:64);}
 r.read_bytes=0;assert(nj_from_srw(&r,&j,&why));u32(data+34,8);r.read_bytes=0;assert(nj_from_srw(&r,&j,&why));
 u16(data+8,513);r.read_bytes=0;assert(!nj_from_srw(&r,&j,&why));u16(data+8,2);u32(data+4,0xfffffff0u);r.read_bytes=0;assert(!nj_from_srw(&r,&j,&why));
 struct lp_config l={0};l.magic=LP_MAGIC;l.schema=1;l.bytes=sizeof l;l.key=lp_catalog[0].key;memcpy(l.source_packet,lp_catalog[0].packet,272);memcpy(l.profile_key,lp_catalog[0].profile_key,65);memcpy(l.table_sha256,lp_catalog[0].table_sha,65);lp_seal(&l,source);assert(lp_valid(&l,source));
 for(unsigned i=0;i<sizeof l;i++){((unsigned char*)&l)[i]^=1;assert(!lp_valid(&l,source));((unsigned char*)&l)[i]^=1;}
 l.key.correction[7]^=1;lp_seal(&l,source);assert(!lp_valid(&l,source));l.key=lp_catalog[0].key;l.source_packet[128]^=1;lp_seal(&l,source);assert(!lp_valid(&l,source));
 struct ps_sample s;ps_init(&s,PS_BAYER_OUT,123,source);s.status=PS_OTF_NO_RAM;ps_seal(&s);assert(!ps_valid(&s));s.status=PS_VALID;ps_seal(&s);assert(ps_valid(&s));s.tiles[24][1][255]^=1;assert(!ps_valid(&s));
 printf("PASS bounded JPEG DQT/IFD corruption and read-budget cases; 1024 LDC binding mutations, unsampled key/table rejection and OTF sample exclusion; ps_bytes=%u\n",(unsigned)sizeof s);return 0;
}
