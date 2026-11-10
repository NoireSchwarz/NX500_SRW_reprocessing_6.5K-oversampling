/* Production dimensions and the actual LDC packet builder; file IO only. */
#define _GNU_SOURCE
#define NXL_FAST_PIPELINE
#include <assert.h>
#include <stdio.h>
#include "../src/bitb_job.h"
#include "../src/ldc_engine.h"
int main(void){
 _Static_assert(TEST_W==6480&&TEST_H==4320,"production geometry, not the small model");
 uint32_t packets[52];ldc_dma(packets,0x74000000,0);ldc_dma(packets+26,0x78000000,1);
 FILE*f=fopen("/mnt/mmc/ldc-production-dma.bin","wx");assert(f&&fwrite(packets,1,sizeof packets,f)==sizeof packets&&!fclose(f));
 struct lp_config config;f=fopen("/mnt/mmc/profile.bin","rb");assert(f&&fread(&config,1,sizeof config,f)==sizeof config&&!fclose(f)&&lp_valid(&config,config.source_sha256));
 uint32_t packed[72],params[27];lp_pack(&config,packed);ldc_params(params,0x10006000);
 f=fopen("/mnt/mmc/ldc-production-core.bin","wx");assert(f&&fwrite(params,1,sizeof params,f)==sizeof params&&!fclose(f));
 f=fopen("/mnt/mmc/ldc-production-packed.bin","wx");assert(f&&fwrite(packed,1,sizeof packed,f)==sizeof packed&&!fclose(f));
 puts("PASS actual production LDC packet helper, 6480x4320; no native calls or DMA");return 0;
}
