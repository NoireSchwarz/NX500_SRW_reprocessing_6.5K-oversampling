#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "param_dma_contract.h"

static void completed(uint32_t *v){
 memset(v,0,256);v[0]=P49_PD_MAGIC;v[1]=P49_PD_SCHEMA;v[2]=12345;
 v[3]=P49_PD_CLOSED;v[5]=0x81dfe000;v[6]=0xbb71d300;
 v[7]=P49_PD_SOURCE;v[8]=P49_PD_SIZE;v[9]=1;
 v[11]=0xa0;v[13]=0;v[17]=0xbb701402;v[18]=1;
 v[19]=v[20]=1;v[21]=2;v[23]=v[24]=2;v[25]=120;v[26]=80;
 v[27]=0x81de1000;v[28]=P49_PD_ISR_RETURN;v[29]=3;v[30]=0x28;
 v[31]=v[17];v[32]=P49_PD_SOURCE;v[33]=P49_PD_SIZE;v[34]=1;
 v[36]=0x40a001;v[37]=0xa1c0d000;v[38]=v[39]=1;v[41]=0xa0;
 v[45]=P49_PD_SOURCE;v[46]=P49_PD_SIZE;v[47]=1;
}
int main(void){
 uint32_t v[64],copy[64];completed(v);assert(p49_pd_completion_record_valid(v,12345));
 assert(!P49_PD_INITIAL_DRAIN_PROVEN&&!p49_pd_audit_valid(v,12345));
 assert(!p49_pd_completion_record_valid(v,12346));
 assert(p49_pd_parameter_uncached(0xbb701402));
 /* Reject a cacheable alias, wrong physical section, AP/AF restrictions,
  * coarse-page mapping, supersection and execute-never mapping. */
 const uint32_t bad_pte[]={0xbb70140e,0xbb601402,0xbb701002,0xbb741402,0xbb701403,0xbb701412};
 for(unsigned i=0;i<sizeof bad_pte/sizeof *bad_pte;i++)assert(!p49_pd_parameter_uncached(bad_pte[i]));
 assert(p49_pd_irq_boundary(0x81de1000,P49_PD_ISR_RETURN,3,8));
 assert(!p49_pd_irq_boundary(0x81de1004,P49_PD_ISR_RETURN,3,8));
 assert(!p49_pd_irq_boundary(0x81de1000,P49_PD_ISR_RETURN,5,32));
 assert(!p49_pd_irq_boundary(0x81de1000,P49_PD_ISR_RETURN+4,3,8));
 assert(!p49_pd_irq_boundary(0x81de1000,P49_PD_ISR_RETURN,3,0));
 /* A zero native return and expected DMA registers without two fresh
  * completion generations must never be accepted. */
 const unsigned must_be_exact[]={0,1,2,3,4,6,7,8,9,10,14,15,16,18,19,20,21,22,23,24,28,29,32,33,34,38,39,40,43,44,45,46,47};
 unsigned rejected=0;
 for(unsigned i=0;i<sizeof must_be_exact/sizeof *must_be_exact;i++){
  memcpy(copy,v,256);copy[must_be_exact[i]]^=1;assert(!p49_pd_completion_record_valid(copy,12345));rejected++;
 }
 const unsigned masks[]={11,12,41,42};
 for(unsigned i=0;i<4;i++){memcpy(copy,v,256);copy[masks[i]]|=8;assert(!p49_pd_completion_record_valid(copy,12345));rejected++;}
 for(unsigned i=48;i<64;i++)for(unsigned bit=0;bit<32;bit++){
  memcpy(copy,v,256);copy[i]=1u<<bit;assert(!p49_pd_completion_record_valid(copy,12345));rejected++;
 }
 memcpy(copy,v,256);copy[25]=P49_PD_MAX_POLLS+1;assert(!p49_pd_completion_record_valid(copy,12345));
 memcpy(copy,v,256);copy[31]^=4;assert(!p49_pd_completion_record_valid(copy,12345));
 uint32_t factory[64]={P49_PD_MAGIC,P49_PD_SCHEMA,12345,P49_PD_SKIPPED_FACTORY_CC};
 factory[P49_PD_W_PROFILE_MODE]=P49_PD_MODE_FACTORY_CC;
 assert(p49_pd_factory_audit_valid(factory,12345));
 assert(!p49_pd_factory_audit_valid(factory,12346));
 assert(!p49_pd_factory_audit_valid(v,12345));
 assert(!p49_pd_completion_record_valid(factory,12345));
 /* Factory skip must never carry fabricated callback/count/commit evidence. */
 for(unsigned i=4;i<64;i++)if(i!=P49_PD_W_PROFILE_MODE)for(unsigned bit=0;bit<32;bit++){
  memcpy(copy,factory,256);copy[i]=1u<<bit;assert(!p49_pd_factory_audit_valid(copy,12345));rejected++;
 }
 memcpy(copy,factory,256);copy[P49_PD_W_PROFILE_MODE]^=1;assert(!p49_pd_factory_audit_valid(copy,12345));
 printf("PASS parameter DMA: original NC mapping, fresh IRQ boundaries, %u stale/failed/corrupt records rejected\n",rejected);
 return 0;
}
