
#include "../src/native_allocator.inc"
#include <assert.h>
int main(int argc,char**argv){assert(argc==3);FILE*f=fopen("/allocator.bin","rb");assert(f);unsigned char*mm=calloc(1,0xb2000);manager_map=mm;unsigned char*rr=calloc(1,4096);root_map=rr;assert(mm&&rr);assert(fread(mm+0x908,1,720896,f)==720896&&!fclose(f));
#define PUT(p,v) do{uint32_t value=(v);memcpy((p),&value,4);}while(0)
 PUT(rr+0x498,0x80574230);PUT(rr+0x49c,0x805742ac);PUT(rr+0x4bc,MEM_MANAGER);PUT(mm+0x800,0x80574b60);PUT(mm+0x800+0x104,0x80574c48);PUT(mm+0x800+0xb0108,0x94000000);PUT(mm+0x800+0xb010c,ALLOC_BLOCKS);PUT(mm+0x800+0xb0110,(uint32_t)strtoul(argv[2],NULL,0));PUT(mm+0x800+0xb05a0,0xbb6cc000);PUT(mm+0x800+CAP_MANAGER_BANK2_OFFSET,0x9c1b0000);
 log_file=fopen("/capacity.log","wx");assert(log_file);int ok=snapshot("/mnt/mmc");assert(ok==atoi(argv[1]));assert(!native_calls&&!request_done&&!ownership_verified&&!api_inflight);printf("SNAPSHOT pass=%d bytes=%u reason=%s native_calls=%u\n",ok,ALLOC_BYTES,allocator_snapshot_reason,native_calls);return 0;}
