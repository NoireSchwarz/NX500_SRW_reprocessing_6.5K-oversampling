#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include "../../common/ldc_observation.h"
static uint32_t top[2048],ldc[1024];static unsigned mode,clocks,ldc_reads,debug_reads;
static int read_only(void*v,unsigned bank,uint32_t off,void*out,unsigned bytes){
 (void)v;assert(lh_known_span(bank,off,bytes));if(bank){ldc_reads++;if(off==0x408){debug_reads++;if(mode==3)return 0;}if(mode==6&&off==0)return 0;}
 if(!bank&&off==0x1020&&bytes==8){if(mode==5&&!clocks++)return 0;if(mode!=5&&clocks++==2&&mode==2)top[0x1020/4]=8;}
 memcpy(out,(bank?ldc:top)+off/4,bytes);return 1;
}
int main(void){
 for(mode=0;mode<7;mode++){
  memset(top,0,sizeof top);for(unsigned i=0;i<1024;i++)ldc[i]=0x10000000u+i;clocks=ldc_reads=debug_reads=0;if(mode==1)top[0x1020/4]=8;
  struct lh_snapshot s;lh_capture(read_only,NULL,mode==4?0x400:0x1000,3,&s);assert(s.schema==1&&s.bytes==sizeof s&&s.phase==3);
  if(!mode)assert(s.flags==15&&debug_reads==1&&s.debug[0]==ldc[0x408/4]);
  if(mode==1)assert(!ldc_reads&&(s.flags&LH_GATED)&&!(s.flags&(LH_CONFIG_OK|LH_DEBUG_OK)));
  if(mode==2)assert((s.flags&LH_CLOCK_CHANGED)&&!(s.flags&(LH_CLOCK_STABLE|LH_CONFIG_OK|LH_DEBUG_OK)));
  if(mode==3)assert((s.flags&LH_READ_FAILED)&&(s.flags&LH_CONFIG_OK)&&!(s.flags&LH_DEBUG_OK));
  if(mode==4)assert(!debug_reads&&(s.flags&LH_DEBUG_OUTSIDE_MAPPING)&&(s.flags&LH_CONFIG_OK));
  if(mode==5)assert(!ldc_reads&&s.flags==LH_READ_FAILED);
  if(mode==6)assert((s.flags&LH_READ_FAILED)&&!(s.flags&LH_CONFIG_OK)&&(s.flags&LH_DEBUG_OK));
  printf("PASS RO LDC clock/mapping/read-fault observation case=%u flags=%u\n",mode,s.flags);
 }
 assert(!lh_known_span(0,0x1004,4)&&!lh_known_span(1,0x200,4)&&!lh_known_span(1,0x454,8));return 0;
}
