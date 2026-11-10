#define _GNU_SOURCE
#include <assert.h>
#include <stdlib.h>
#include "srw_iq.h"
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);uint32_t v[256];int ok=p41_srw_info(f,v);assert(!fclose(f));
 if(!ok){puts("REJECT");return 2;}
 printf("{\"iso\":%u,\"wb_q8\":[%u,%u,%u,%u],\"black\":[%u,%u,%u,%u],\"matrix\":[",v[4],v[8],v[9],v[10],v[11],v[16],v[17],v[18],v[19]);
 for(unsigned i=0;i<27;i++)printf("%s%d",i?",":"",(int32_t)v[20+i]);puts("]}");return 0;
}
