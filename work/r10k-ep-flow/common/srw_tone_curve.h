/* SPDX-License-Identifier: AGPL-3.0-only. A040/A042 are evidence only.
 * Source-relative offsets; bounded reads; no curve is submitted to the ISP. */
#ifndef NXL_SRW_TONE_CURVE_H
#define NXL_SRW_TONE_CURVE_H
#include <stdio.h>
#include "lens_profile.h"
struct stc_source {
 uint32_t status,le,maker,maker_end,available,read_bytes;
 uint32_t offsets[3],key[11],raw[2][23],x[2][11],y[2][11];
};
/* status:1 decoded;2 missing/unsupported/malformed. available bits key/A040/A042. */
static int stc_from_srw(struct nj_reader*r,struct stc_source*s){
 unsigned char h[8],table[6144];struct lp_entry e[3];memset(s,0,sizeof *s);memset(e,0,sizeof e);s->status=2;
 if(!nj_read(r,0,h,8)||!((h[0]=='I'&&h[1]=='I')||(h[0]=='M'&&h[1]=='M')))goto done;
 s->le=h[0]=='I';if(nj_u16(h+2,s->le)!=42||!lp_maker(r,nj_u32(h+4,s->le),s->le,&s->maker,&s->maker_end)||!nj_read(r,s->maker,h,2))goto done;
 unsigned count=nj_u16(h,s->le);if(count>512||(uint64_t)s->maker+2+count*12+4>s->maker_end||!nj_read(r,(uint64_t)s->maker+2,table,count*12))goto done;
 const uint16_t wanted[3]={0xa020,0xa040,0xa042};unsigned seen=0;
 for(unsigned i=0;i<count;i++)for(unsigned k=0;k<3;k++)if(nj_u16(table+i*12,s->le)==wanted[k]){
  if(seen&(1u<<k))goto done;seen|=1u<<k;const unsigned char*p=table+i*12;
  e[k].type=nj_u16(p+2,s->le);e[k].count=nj_u32(p+4,s->le);
  uint64_t at=(uint64_t)s->maker+nj_u32(p+8,s->le);if(at>0xffffffffu)goto done;e[k].at=(uint32_t)at;s->offsets[k]=(uint32_t)at;
 }
 if(seen!=7u||!lp_array(r,e,4,11,s->le,s->maker,s->maker_end,s->key))goto done;s->available=1;
 for(unsigned k=0;k<2;k++){
  if(!lp_array(r,e+k+1,4,23,s->le,s->maker,s->maker_end,s->raw[k])||s->raw[k][0]!=11u)goto done;
  s->available|=2u<<k;
  for(unsigned i=0;i<11;i++){
   /* Unsigned arithmetic deliberately wraps modulo2^32. Salt restarts for Y. */
   s->x[k][i]=s->raw[k][i+1]+s->key[i];s->y[k][i]=s->raw[k][i+12]-s->key[i];
   if(s->x[k][i]>16383u||s->y[k][i]>255u||(i&&s->x[k][i]<=s->x[k][i-1]))goto done;
  }
 }s->status=1;
done:s->read_bytes=r->read_bytes;return s->status==1;
}
static void stc_array(FILE*f,const uint32_t*v,unsigned n){for(unsigned i=0;i<n;i++)fprintf(f,"%s%u",i?",":"",v[i]);}
static int stc_report(FILE*f,const struct stc_source*s,const char*source){
 fprintf(f,"{\"schema\":1,\"release\":\"R8m\",\"source_sha256\":\"%s\",\"status\":%u,\"available\":%u,\"byte_order\":\"%s\",\"maker\":%u,\"maker_end\":%u,\"read_bytes\":%u,\"observation_only\":true,\"curve_applied\":false,\"native_adoption_proven\":false,\"A020_offset\":%u,\"key\":[",source,s->status,s->available,s->le?"little":"big",s->maker,s->maker_end,s->read_bytes,s->offsets[0]);stc_array(f,s->key,11);fputs("],\"curves\":[",f);
 for(unsigned k=0;k<2;k++){
  fprintf(f,"%s{\"tag\":\"%s\",\"offset\":%u,\"raw\":[",k?",":"",k?"A042":"A040",s->offsets[k+1]);stc_array(f,s->raw[k],23);fputs("],\"x\":[",f);stc_array(f,s->x[k],11);fputs("],\"y\":[",f);stc_array(f,s->y[k],11);fputs("]}",f);
 }return fputs("]}\n",f)>=0&&!ferror(f);
}
#endif
