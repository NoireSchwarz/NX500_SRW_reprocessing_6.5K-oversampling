/* Production admission and invalid-format checks with modeled RAM reads. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../../common/slog8_contract.h"
#include "slog8_neutral_fixture.h"
static unsigned char table[19712];static unsigned reads,fail_at;
static int physical_read(void*c,uint32_t at,void*out,unsigned n){
 (void)c;reads++;assert(at>=0x81119f00u&&(uint64_t)at+n<=0x81119f00ULL+sizeof table&&n&&n<=4096);
 if(reads==fail_at)return 0;memcpy(out,table+at-0x81119f00u,n);return 1;
}
#include "../src/slog8_admission.inc"
int main(void){unsigned cases=0;memcpy(table,neutral_fixture,sizeof table);reads=0;assert(slog8_neutral_slot()&&reads==5);cases++;
 for(unsigned page=0;page<5;page++){unsigned at=page*4096;table[at]^=1;reads=0;assert(!slog8_neutral_slot());table[at]^=1;cases++;}
 for(unsigned fault=1;fault<=5;fault++){reads=0;fail_at=fault;assert(!slog8_neutral_slot()&&reads==fault);cases++;}fail_at=0;
 uint32_t source[10]={0},out[10];assert(!slog8_compose(out,source));cases++;
 /* Two identical neutral calibration blocks, with opaque padding set. */
 for(unsigned i=0;i<10;i++)source[i]=0xf800f800u;
 for(unsigned group=0;group<2;group++)for(unsigned row=0;row<3;row++){
  unsigned half=group*10+row*3,word=half/2,shift=(half&1)*16;
  source[word]|=128u<<shift;
 }
 assert(slog8_compose(out,source));for(unsigned i=0;i<10;i++)assert(out[i]==source[i]);cases++;
 for(unsigned group=0;group<2;group++){
  uint32_t bad[10];memcpy(bad,source,sizeof bad);unsigned word=group*5;bad[word]=(bad[word]&~2047u)|2047u;
  assert(!slog8_compose(out,bad));cases++;
 }
 printf("PASS production SLOG8 admission/format cases=%u\n",cases);return 0;
}
