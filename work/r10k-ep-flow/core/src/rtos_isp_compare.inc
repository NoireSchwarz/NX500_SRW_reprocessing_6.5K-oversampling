/* Only the fixed control regions below are read. DDR access is conditional
 * on the pinned identity Normal-NC page-table contract in ic_capture. */
#include "../../common/isp_compare.h"
static __attribute__((noinline,section(".text.param"))) int ic_rtos_read(void *unused,uint32_t at,void *out,unsigned bytes){
 (void)unused;if(!bytes||(bytes&3u)||(at&3u)||bytes>512u)return 0;
 int ok=(at==0x8153e6d4u||at==0x8153e6d4u+3364u||at==0x81543d40u||at==0x8153e6d4u+3428u||at==0x81002edcu)&&bytes==4u;
 ok|=(at==0x20810570u||at==0x20810770u||at==0x208107d4u)&&bytes==24u;
 ok|=(at>=0xbb71c000u&&(uint64_t)at+bytes<=0xbb71d700ULL)||(at>=0xbb71e000u&&(uint64_t)at+bytes<=0xbb71f700ULL);
 if(!ok)return 0;uint32_t *d=out;for(unsigned i=0;i<bytes/4;i++)d[i]=*ptr(at+i*4);return 1;
}
static __attribute__((noinline,section(".text.param"))) void isp_audit(unsigned stage){
 if(!stage||stage>IC_STAGES)return;
 struct ic_sample *s=(struct ic_sample*)(uintptr_t)(ctx.cap+IC_CAP_OFFSET+(stage-1)*sizeof(struct ic_sample));
 (void)ic_capture(ic_rtos_read,0,s,ctx.nonce,stage);barrier();
}
_Static_assert(IC_CAP_OFFSET>=P39_SOURCE_OFFSET+P39_SOURCE_BYTES+16384u,"audit after input guard");
_Static_assert(IC_CAP_OFFSET+sizeof(struct ic_sample)*IC_STAGES<=P39_TARGET_OFFSET-16384u,"audit before output guard");
