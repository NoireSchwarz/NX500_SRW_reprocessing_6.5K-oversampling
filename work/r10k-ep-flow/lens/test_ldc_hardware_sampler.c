#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../common/ldc_observation.h"
static uint32_t top[1024],ldc[1024];static uint64_t clock_ns=1000000000u;static unsigned records,maps,loads;static struct lh_snapshot latest;
static int dev=7,directory=-1;static FILE*context_file;
static uint64_t now(void){return clock_ns;}
static void*readonly_map(void*addr,size_t bytes,int prot,int flags,int fd,off_t offset){
 assert(!addr&&bytes==4096&&prot==PROT_READ&&flags==MAP_SHARED&&fd==7);maps++;assert(offset==0x20821000||offset==0x20823000);return offset==0x20821000?(void*)top:(void*)ldc;
}
static int readonly_unmap(void*addr,size_t bytes){assert((addr==top||addr==ldc)&&bytes==4096);return 0;}
static int lens_ro_load(const volatile void*from,void*out,unsigned n){loads++;memcpy(out,(const void*)from,n);return 1;}
static int frame(FILE*f,unsigned kind,unsigned seq,uint64_t begin,uint64_t end,const void*data,unsigned bytes){(void)f;(void)seq;assert(kind==15&&begin<=end&&bytes==sizeof latest);latest=*(const struct lh_snapshot*)data;records++;return 1;}
#define mmap readonly_map
#define munmap readonly_unmap
#include "ldc-hardware-capture.inc"
#undef mmap
#undef munmap
int main(void){
 lens_ldc_hardware_sample(0,0);assert(records==1&&maps==2&&latest.phase==0&&latest.flags==15);unsigned prior_loads=loads;
 clock_ns+=2000000;ldc[0x408/4]++;lens_ldc_hardware_sample(1,0);assert(records==1&&loads==prior_loads);
 lens_ldc_hardware_sample(2,1);assert(records==2&&latest.phase==10);clock_ns+=2000000;ldc[0x408/4]++;lens_ldc_hardware_sample(3,1);assert(records==3);
 clock_ns+=2000000;lens_ldc_hardware_sample(4,0);assert(records==4&&latest.phase==0);
 for(unsigned i=0;i<20;i++){clock_ns+=20000000;ldc[0x408/4]++;lens_ldc_hardware_sample(i+5,0);}assert(records==4);
 clock_ns+=1000000000;lens_ldc_hardware_sample(26,0);assert(records==5);
 clock_ns+=20000000;ldc[4]++;lens_ldc_hardware_sample(27,0);assert(records==6);
 clock_ns+=20000000;top[0x20/4]=8;lens_ldc_hardware_sample(28,0);assert(records==7&&(latest.flags&LH_GATED));
 unsigned before=loads;clock_ns+=20000000;lens_ldc_hardware_sample(29,0);assert(records==7&&loads>before);
 FILE*f=fopen("/mnt/mmc/ldc-hardware-schema.json","wx");assert(f);lens_ldc_hardware_schema(f);assert(!ferror(f)&&!fclose(f));
 f=fopen("/mnt/mmc/native-hw-frame.bin","wx");assert(f&&fwrite(&latest,1,sizeof latest,f)==sizeof latest&&!fclose(f));
 lens_ldc_hardware_end();puts("PASS actual native LDC sampler: RO-only mappings, active cadence, inactive suppression, transitions and gated omission");return 0;
}
