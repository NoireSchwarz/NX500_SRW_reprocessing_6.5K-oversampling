#include <assert.h>
#include <stdio.h>
#include "alloc_core.h"
#include "handoff_core.h"
int main(void){
 assert(ALLOC_BYTES==P39_CAP_BYTES&&ALLOC_SPAN_BLOCKS==8192&&sizeof(struct p39_result)==256);
 assert(p39_extent(0xa1c00000)&&p39_extent(0xb8000000)&&!p39_extent(0xb8004000));
 assert(p39_section_kind(0xa1c01402,0xa1c00000)==1&&p39_section_kind(0x8100140e,P39_SLOT)==2);
 uint32_t payload[3];assert(p39_cache_packet(0xa1c00000,123,payload)&&payload[1]==0xa1c10000&&payload[2]==57938944);
 assert(p39_fnv("123456789",9)==0xbb86b11cu);
 assert(P39_TARGET_OFFSET+P39_TARGET_BYTES+16384<P39_CAP_BYTES);
 puts("PASS ARM allocation, full frame cache payload,14x3 geometry,hash vector and extents");return 0;
}
