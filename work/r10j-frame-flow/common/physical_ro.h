/* Only RTOS RAM and the existing Still-idle MMIO allowlist. mmap handles
 * camera RAM that the kernel's /dev/mem read operation rejects with EFAULT. */
#ifndef NXL_PHYSICAL_RO_H
#define NXL_PHYSICAL_RO_H
#include <sys/mman.h>
#include <errno.h>
#include "../core/src/oem_ro_read.h"
struct pr_page {uint32_t base;void*map;};
struct pr_reader {int fd;struct pr_page pages[8];unsigned count,last_address,last_bytes;int last_errno,last_signal;};
static void pr_close(struct pr_reader*r){for(unsigned i=0;i<r->count;i++)munmap(r->pages[i].map,4096);r->count=0;}
static int pr_read(struct pr_reader*r,uint32_t at,void*out,unsigned bytes){
 r->last_address=at;r->last_bytes=bytes;r->last_errno=r->last_signal=0;
 unsigned width=oem_ram_window(at,bytes)?1:(bytes==4&&oem_mmio_address(at)?4:0);if(!width){r->last_errno=EINVAL;return 0;}
 uint32_t base=at&~4095u;void*p=NULL;for(unsigned i=0;i<r->count;i++)if(r->pages[i].base==base)p=r->pages[i].map;
 if(!p){if(r->count==8){r->last_errno=ENOSPC;return 0;}p=mmap(NULL,4096,PROT_READ,MAP_SHARED,r->fd,(off_t)(uint64_t)base);if(p==MAP_FAILED){r->last_errno=errno;return 0;}r->pages[r->count++]=(struct pr_page){base,p};}
 struct oem_ro_fault fault={0};int ok=oem_ro_guarded_load((volatile unsigned char*)p+(at&4095u),out,bytes,width,&fault);
 if(!ok){r->last_signal=fault.signal;r->last_errno=EFAULT;}return ok;
}
#endif
