/* Aligned volatile word reads of ordinary RTOS allocator RAM. No state write,
 * allocator call or freed CAP alias access. Keep the strict proof separate. */
#ifndef NXL_ALLOCATOR_OBSERVATION_H
#define NXL_ALLOCATOR_OBSERVATION_H
#include <stdint.h>
#define AR_TABLE_WORDS (45056u*4u)
#define AR_WAIT_NS 3000000000ULL
#define AR_SAMPLE_LIMIT 1024u
static int ar_copy_words(uint32_t*out,const volatile void*source,unsigned words){
 if(!out||!source||!words||words>AR_TABLE_WORDS||((uintptr_t)out&3u)||((uintptr_t)source&3u))return 0;
 const volatile uint32_t*p=source;for(unsigned i=0;i<words;i++)out[i]=p[i];return 1;
}
#endif
