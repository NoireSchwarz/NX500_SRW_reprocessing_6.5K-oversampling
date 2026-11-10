#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../common/lens_profile.h"
static int read_at(void*ctx,uint64_t at,void*p,unsigned n){return !fseek((FILE*)ctx,(long)at,SEEK_SET)&&fread(p,1,n,ctx)==n;}
int main(void){
 char source[68]={0};memset(source,'a',64);struct lp_config c,changed;
 FILE*out=fopen("/mnt/mmc/generated.bin","wb");assert(out);
 unsigned exact=0,mutations=0,interpolated=0;
 for(unsigned i=0;i<LC_CALIBRATION_COUNT;i++){
  const char*reason=NULL;assert(lc_resolve(&lp_catalog[i].key,&c,&reason));
  assert(c.schema==2&&c.reserved[1]==(i==7?LC_FOCAL:LC_EXACT)&&lp_approximate(&c)==(i==7)&&!memcmp(c.source_packet,lp_catalog[i].packet,272));
  lp_seal(&c,source);assert(lp_valid(&c,source));assert(fwrite(&c,1,sizeof c,out)==sizeof c);exact+=i!=7;
  for(unsigned j=0;j<sizeof c;j++){changed=c;((unsigned char*)&changed)[j]^=1;assert(!lp_valid(&changed,source));mutations++;}
  changed=c;changed.source_packet[15]^=1;lp_seal(&changed,source);assert(!lp_valid(&changed,source));
  changed=c;changed.reserved[5]^=1;lp_seal(&changed,source);assert(!lp_valid(&changed,source));
 }
 for(unsigned f=240;f<=770;f++){
  struct lp_key key=lp_catalog[3].key;key.focal35x10=f;
  assert(lc_resolve(&key,&c,NULL));lp_seal(&c,source);assert(lp_valid(&c,source));
  assert(fwrite(&c,1,sizeof c,out)==sizeof c);interpolated++;
 }
 for(unsigned f=239;f<=771;f+=532){struct lp_key k=lp_catalog[3].key;k.focal35x10=f;assert(!lc_resolve(&k,&c,NULL));}
 struct lp_key key=lp_catalog[3].key;key.lens++;assert(!lc_resolve(&key,&c,NULL));
 key=lp_catalog[3].key;key.serial[0]^=1;assert(lc_resolve(&key,&c,NULL)&&lp_approximate(&c));
 key=lp_catalog[3].key;key.correction[30]^=1;assert(lc_resolve(&key,&c,NULL)&&lp_approximate(&c));
 /* All observed35mm metadata states intentionally use ONE geometric table. */
 for(unsigned i=6;i<=7;i++){
  key=lp_catalog[i].key;key.correction[29]^=1;
  assert(lc_resolve(&key,&c,NULL)&&c.reserved[1]==LC_FOCAL);
  assert(!memcmp(c.source_packet,lp_catalog[i].packet,272));
 }
 const char*files[]={"/mnt/mmc/SAM_0379.SRW","/mnt/mmc/SAM_0380.SRW"};
 for(unsigned i=0;i<2;i++){
  FILE*f=fopen(files[i],"rb");assert(f&&!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);
  struct nj_reader r={f,(uint64_t)size,read_at,0};const char*reason=NULL;
  assert(lp_from_srw(&r,&c,&reason));lp_seal(&c,source);assert(lp_valid(&c,source));assert(!fclose(f));
  assert(c.reserved[1]==(i?LC_NOTE:LC_EXACT));assert(fwrite(&c,1,sizeof c,out)==sizeof c);
  printf("SOURCE %u %s %s %u\n",i,lp_method(&c),c.table_sha256,r.read_bytes);
 }
 assert(!fclose(out));printf("PASS exact=%u mutations=%u focal_sweep=%u source_files=2\n",exact,mutations,interpolated);return 0;
}
