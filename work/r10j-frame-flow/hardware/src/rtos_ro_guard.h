/* SPDX-License-Identifier: AGPL-3.0-only. Match existing RTOS code checks:
 * map physical P7 pages read-only. /dev/mem file reads and mmap are distinct
 * kernel interfaces; do not assume a successful Linux-RAM pread covers P7. */
#ifndef NXL_RTOS_RO_GUARD_H
#define NXL_RTOS_RO_GUARD_H
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdio.h>
struct rtos_ro_guard_audit {
 uint32_t address,page,offset,valid,map_success,map_errno,read_success,matched,first_mismatch,unmap_success,unmap_errno,actual[8];
};
static int rtos_ro_code_guard(int fd,uint32_t address,const uint32_t expected[8],struct rtos_ro_guard_audit*a){
 memset(a,0,sizeof *a);a->address=address;a->page=address&~4095u;a->offset=address&4095u;a->first_mismatch=UINT32_MAX;
 if(fd<0||!expected||(address&3u)||a->offset>4096u-32u)return 0;a->valid=1;
 errno=0;void*p=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,(off_t)(uint64_t)a->page);
 if(p==MAP_FAILED){a->map_errno=(uint32_t)errno;return 0;}a->map_success=1;
 volatile const uint32_t*words=(volatile const uint32_t*)((const unsigned char*)p+a->offset);
 __sync_synchronize();a->matched=1;
 for(unsigned i=0;i<8;i++){a->actual[i]=words[i];if(a->actual[i]!=expected[i]){if(a->matched)a->first_mismatch=i;a->matched=0;}}
 __sync_synchronize();a->read_success=1;errno=0;
 if(munmap(p,4096))a->unmap_errno=(uint32_t)errno;else a->unmap_success=1;
 return a->matched&&a->unmap_success;
}
static void rtos_ro_guard_log(FILE*f,const char*name,const uint32_t expected[8],const struct rtos_ro_guard_audit*a){
 if(!f)return;
 fprintf(f,"WORKER_PREPARE_GUARD name=%s read_method=MMAP_READ_ONLY address=0x%x page=0x%x offset=0x%x valid=%u map_success=%u map_errno=%u read_success=%u matched=%u first_mismatch=%u unmap_success=%u unmap_errno=%u expected=",name,a->address,a->page,a->offset,a->valid,a->map_success,a->map_errno,a->read_success,a->matched,a->first_mismatch,a->unmap_success,a->unmap_errno);
 for(unsigned i=0;i<8;i++)fprintf(f,"%s%08x",i?",":"",expected[i]);fputs(" actual=",f);
 for(unsigned i=0;i<8;i++)fprintf(f,"%s%08x",i?",":"",a->actual[i]);fputc('\n',f);
}
#endif
