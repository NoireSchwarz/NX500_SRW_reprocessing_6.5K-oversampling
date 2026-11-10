#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <sys/stat.h>
#include "../../common/srw_tone_curve.h"
static int load(void*v,uint64_t at,void*p,unsigned n){FILE*f=v;return !fseeko(f,(off_t)at,SEEK_SET)&&fread(p,1,n,f)==n;}
int main(int argc,char**argv){
 assert(argc==3);FILE*f=fopen(argv[1],"rb");assert(f);struct stat st;assert(!fstat(fileno(f),&st));
 struct nj_reader r={f,(uint64_t)st.st_size,load,0};struct stc_source s;(void)stc_from_srw(&r,&s);assert(s.read_bytes<=262144);assert(!fclose(f));
 f=fopen("/source-tone-curves.json","wb");assert(f&&stc_report(f,&s,argv[2])&&!fclose(f));printf("status=%u available=%u bytes=%u\n",s.status,s.available,s.read_bytes);return 0;
}
