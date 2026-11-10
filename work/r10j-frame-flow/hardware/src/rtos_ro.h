/* SPDX-License-Identifier: AGPL-3.0-only; fixed-range read-only RTOS access. */
#ifndef NXL_RTOS_RO_H
#define NXL_RTOS_RO_H
#define RTOS_RO_PAGE_COUNT 6u
static const uint32_t rtos_ro_pages[RTOS_RO_PAGE_COUNT]={0x803fb000u,0x804b6000u,0x804bb000u,0x804cf000u,0x817d6000u,0x817d8000u};
struct rtos_ro {volatile const uint32_t *page[RTOS_RO_PAGE_COUNT];};
static int rtos_ro_allowed(uint32_t address,uint32_t bytes){
 if((address&3)||(bytes&3))return 0;
 for(unsigned i=0;i<sizeof h3dl_rtos_guards/sizeof h3dl_rtos_guards[0];i++)
  if(address==h3dl_rtos_guards[i].address&&bytes==32)return 1;
 return (address==H3DL_RTOS_IRQ_BASE&&bytes==544)||(address==H3DL_RTOS_STATE_BASE&&bytes==20);
}
static int rtos_ro_read(const struct rtos_ro *r,uint32_t address,void *out,uint32_t bytes){
 if(!out||!rtos_ro_allowed(address,bytes)||(address&4095)+bytes>4096)return 0;
 for(unsigned i=0;i<RTOS_RO_PAGE_COUNT;i++)if(rtos_ro_pages[i]==(address&~4095u)){
  if(!r->page[i])return 0;
  uint32_t *p=out;for(unsigned j=0;j<bytes/4;j++)p[j]=r->page[i][((address&4095)/4)+j];return 1;
 }
 return 0;
}
#endif
