
#define NXL_FAST_PIPELINE
#include <assert.h>
#include <stdio.h>
#include "../../hardware/src/bitb_job.h"
#include "../../hardware/src/ldc_engine.h"
#include "../src/front_layout.h"
int main(void){
 _Static_assert(SW_BYTES==184549376u&&P39_CAP_BYTES==SW_BYTES,"unified176MiB");
 _Static_assert(SW_LDC_OUT==125829120u&&SW_FRONT_ACTIVE==134217728u,"LDC120/frontcache128");
 _Static_assert(P39_SOURCE_OFFSET+P39_SOURCE_BYTES+16384u<SW_INPUT,"RAW/NV16 isolation");
 _Static_assert(SW_INPUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H+16384u<SW_LDC_OUT,"live ISP/LDC isolation");
 _Static_assert(SW_LDC_OUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H+16384u<SW_TABLE,"LDC/table isolation");
 uint32_t a[26],b[26];ldc_dma(a,0x70000000u+SW_INPUT,0);ldc_dma(b,0x70000000u+SW_LDC_OUT,1);
 assert(a[0]==0x70000000u+SW_INPUT&&b[0]==0x70000000u+SW_LDC_OUT&&a[1]==a[0]+SW_FRONT_UV&&b[1]==b[0]+SW_FRONT_UV);
 printf("PRODUCTION_LAYOUT cap=%u input=%u ldc=%u front_end=%u ldc_end=%u table=%u width=%u height=%u\n",SW_BYTES,SW_INPUT,SW_LDC_OUT,SW_INPUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H,SW_LDC_OUT+SW_FRONT_UV+SW_FRONT_STRIDE*SW_H,SW_TABLE,SW_W,SW_H);return 0;}
