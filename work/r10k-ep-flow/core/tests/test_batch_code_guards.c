/* Production read-only guard reader with bounded modeled mmap pages. */
#define _GNU_SOURCE
#include <stddef.h>
#include <sys/types.h>
static void *guard_map(void*,size_t,int,int,int,off_t);
static int guard_unmap(void*,size_t);
#define P33_GUARD_MMAP guard_map
#define P33_GUARD_MUNMAP guard_unmap
#define main production_main_not_run
#ifdef NXL_GUARD_REPLAY_LEGACY
#include "../../../r9b-source-cc/core/src/nxl-native-front.c"
#else
#include "nxl-native-front.c"
#endif
#undef main
#include <assert.h>
static unsigned char page[8192];static uint32_t bad_address;static unsigned fail_map_cross,fail_unmap_cross,last_map_bytes;
static void *guard_map(void *a,size_t n,int prot,int flags,int fd,off_t base){
 (void)a;(void)fd;assert((n==4096||n==8192)&&prot==PROT_READ&&flags==MAP_SHARED&&!(base&4095));last_map_bytes=(unsigned)n;
 if(n==8192&&fail_map_cross){errno=EACCES;return MAP_FAILED;}
 FILE*f=fopen("/firmware/P7.bin","rb");assert(f&&!fseeko(f,base-0x80000000u,SEEK_SET)&&fread(page,1,n,f)==n&&!fclose(f));
 if(bad_address&&bad_address>=(uint64_t)base&&bad_address<(uint64_t)base+n)page[bad_address-(uint32_t)base]^=1;return page;
}
static int guard_unmap(void *p,size_t n){assert(p==page&&n==last_map_bytes);if(n==8192&&fail_unmap_cross){errno=EIO;return -1;}return 0;}
int main(int argc,char **argv){
 assert(argc==2);devfd=open("/dev/null",O_RDONLY);assert(devfd>=0);bad_address=0;
#ifdef NXL_GUARD_REPLAY_LEGACY
 assert(!ro_native_guards(argv[1])&&!rtos_readonly_guards&&!strcmp(bridge_error,"RTOS_GUARD_FIXED_RANGE_INVALID"));assert(!native_calls&&!allocation_requests&&!send_calls&&!api_inflight);assert(!close(devfd));puts("PASS replay R9b actual guard reader: original P7 bytes reach cross-page fixed-range rejection before native activity");return 0;
#else
 uint32_t base;unsigned bytes;assert(ro_guard_layout(0x801d9fe0u,&base,&bytes)&&bytes==4096);assert(ro_guard_layout(0x801d9fe4u,&base,&bytes)&&base==0x801d9000u&&bytes==8192);assert(ro_guard_layout(0x801d9ffcu,&base,&bytes)&&bytes==8192);assert(ro_guard_layout(0x80ffffe0u,&base,&bytes)&&bytes==4096);assert(!ro_guard_layout(0x80ffffe4u,&base,&bytes));assert(!ro_guard_layout(0x801d9fe5u,&base,&bytes));assert(!ro_guard_layout(0x7ffffffcu,&base,&bytes));assert(!ro_guard_layout(0xfffffffcu,&base,&bytes));
 assert(ro_native_guards(argv[1])&&rtos_readonly_guards&&rtos_guard_crosspage==1&&rtos_guard_samples==sizeof p33_rtos_guards/sizeof *p33_rtos_guards&&rtos_guard_mappings==rtos_guard_unmapped);
 char original[1200],saved[1200];snprintf(original,sizeof original,"%s/p39_rtos_code_guards.txt",argv[1]);snprintf(saved,sizeof saved,"%s/success-all-original-guards.txt",argv[1]);assert(!rename(original,saved));
 bad_address=p33_rtos_guards[0].offset;assert(!ro_native_guards(argv[1])&&!rtos_readonly_guards&&!strcmp(bridge_error,"RTOS_GUARD_BYTE_MISMATCH"));
 bad_address=p33_rtos_guards[sizeof p33_rtos_guards/sizeof *p33_rtos_guards-1].offset;assert(!ro_native_guards(argv[1])&&!rtos_readonly_guards&&!strcmp(bridge_error,"RTOS_GUARD_BYTE_MISMATCH"));
 bad_address=0x801da000u;assert(!ro_native_guards(argv[1])&&!rtos_readonly_guards&&!strcmp(bridge_error,"RTOS_GUARD_BYTE_MISMATCH")&&rtos_guard_crosspage==1&&rtos_guard_mappings==rtos_guard_unmapped);
 bad_address=0;fail_map_cross=1;assert(!ro_native_guards(argv[1])&&!strcmp(bridge_error,"RTOS_GUARD_READONLY_MAP_FAILED")&&rtos_guard_mappings==rtos_guard_unmapped);fail_map_cross=0;
 fail_unmap_cross=1;assert(!ro_native_guards(argv[1])&&!strcmp(bridge_error,"RTOS_GUARD_READONLY_UNMAP_FAILED")&&rtos_guard_mappings==rtos_guard_unmapped+1);fail_unmap_cross=0;
 assert(!close(devfd));devfd=open("/dev/null",O_RDWR);assert(devfd>=0);assert(!ro_native_guards(argv[1])&&!strcmp(bridge_error,"RTOS_GUARD_FD_NOT_READONLY")&&!rtos_guard_mappings);
 assert(!native_calls&&!allocation_requests&&!hook_ever_installed&&!send_calls&&!api_inflight);assert(!close(devfd));puts("PASS production RO guard reader: complete original P7 signatures, crossing page and second-page corruption, map/unmap/permission failures; native counters zero");return 0;
#endif
}
