#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include "session_rearm_contract.h"
static uint32_t t[R7_BLOCKS*4];
int main(void){uint32_t pa=0xa1c00000u,va=0x90ba8000u,ix=(pa-0x94000000u)>>14,n=0;
#define C(x) do{assert(x);n++;}while(0)
C(r7_extent(pa,va));C(!r7_extent(pa+1,va));C(!r7_extent(pa,0));C(!r7_extent(0x94000000u-16384u,va));C(!r7_extent(0xb8004000u,va));C(r7_old_range_free(t,pa,va));C(r7_span_free(t,28055,12288));C(!r7_span_free(t,0,R7_BLOCKS+1));C(!r7_span_free(t,R7_BLOCKS,1));
for(unsigned k=0;k<4;k++){uint32_t pos=ix+(k?R7_OLD_BLOCKS-1:0);t[pos*4]=k?0x23:0x100;t[pos*4+1]=k?1:4;C(!r7_old_range_free(t,pa,va));t[pos*4]=t[pos*4+1]=0;}
t[(ix-1)*4]=0x23;t[(ix-1)*4+1]=2;C(!r7_old_range_free(t,pa,va));t[(ix-1)*4]=t[(ix-1)*4+1]=0;
t[0]=0x23;t[1]=R7_BLOCKS+1;C(!r7_old_range_free(t,pa,va));t[0]=t[1]=0;t[(ix-1)*4]=0x23;t[(ix-1)*4+1]=1;C(r7_old_range_free(t,pa,va));t[(ix-1)*4]=t[(ix-1)*4+1]=0;
uint32_t roots[5]={0,0x800e454c,0x8153e6d4,0x20810000,0xc075551c};C(r7_roots_clear(roots,5,pa,va));for(unsigned k=0;k<4;k++){roots[1]=k==0?pa:k==1?pa+R7_CAP_BYTES-1:k==2?va:va+R7_CAP_BYTES-1;C(!r7_roots_clear(roots,5,pa,va));}roots[1]=pa+R7_CAP_BYTES;C(r7_roots_clear(roots,5,pa,va));
C(r7_native_worker_name("nxl-allocprobe."));C(r7_native_worker_name("nxl-bitbworker."));C(r7_native_worker_name("nxl-hw-controll"));C(r7_native_worker_name("nxl-hw-worker.a"));C(r7_native_worker_name("dfmsd"));C(!r7_native_worker_name("di-camera-app"));C(!r7_native_worker_name("nxl-session-rea"));C(!r7_native_worker_name("bluetoothd"));
puts("PASS pure rearm extent, complete allocator span overlap, stale interior and pointer boundary predicates");printf("checks=%u\n",n);return 0;}
