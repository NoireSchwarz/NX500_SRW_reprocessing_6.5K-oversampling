#include <stdio.h>
#include <assert.h>
#include "../../common/video_mode_patch.h"
#include "../../common/video_native_24p.h"
static struct {unsigned mode,reads,writes,ready,freeze,thaw;uint32_t word;} m;
static int readw(void*c,uint32_t at,uint32_t*out){(void)c;assert(at==V24_WIDTH);m.reads++;if(m.mode==5&&m.reads==3)return 0;*out=m.word^((m.mode==4&&m.reads==3)?1u:0u);return 1;}
static int writew(void*c,uint32_t at,uint32_t word){(void)c;assert(at==V24_WIDTH);m.writes++;if((m.mode==3&&m.writes==1)||(m.mode==8&&m.writes<=2))return 0;m.word=word;return 1;}
static int ready(void*c){(void)c;m.ready++;return m.mode!=1&&!(m.mode==6&&m.ready==2);}
static int freeze(void*c){(void)c;m.freeze++;return m.mode!=2;}
static int thaw(void*c){(void)c;m.thaw++;return m.mode!=7;}
int main(void){struct vm_ops o={0,freeze,thaw,ready,readw,writew};struct vm_result result;unsigned cases=0;
 for(unsigned dir=0;dir<2;dir++)for(unsigned mode=0;mode<9;mode++){
  memset(&m,0,sizeof m);m.mode=mode;m.word=dir?V24_UHD:V24_NATIVE;uint32_t old=m.word;int ok=v24_apply(&o,!dir,&result);assert(ok==(mode==0));assert(m.freeze==m.thaw);
  if(mode==1||mode==2||mode==6)assert(!m.writes);
  if(mode==3||mode==4||mode==5)assert(result.rollback_verified&&m.word==old);
  if(mode==8)assert(result.rollback_attempted&&!result.rollback_verified);
  if(!mode)assert(m.word==(dir?V24_NATIVE:V24_UHD));cases++;
 }
 memset(&m,0,sizeof m);m.word=V24_NATIVE;assert(v24_apply(&o,0,&result)&&!m.writes&&!m.freeze);cases++;
 for(unsigned bit=0;bit<32;bit++){memset(&m,0,sizeof m);m.word=V24_NATIVE^(1u<<bit);if(v24_known(m.word))continue;assert(!v24_apply(&o,1,&result)&&!m.writes&&!m.freeze);cases++;}
 printf("PASS native24 transaction cases=%u; failed writes restore; busy/foreign code refuses; thaw required\n",cases);return 0;}
