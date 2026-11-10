/* Writes only owned scratch and the original bank1 Gamma DDR shadow.
 * Called after native geometry setters, BEFORE the only original config load.
 * Restore only after verified frame+DMA completion, never while in flight. */
#include "gamma_contract.h"
#define GT_FN __attribute__((noinline,section(".text.gamma")))
static volatile uint32_t *gt(void){return ptr(ctx.cap+GT_OFFSET);}
static int gt_roots(void){uint32_t pte=*ptr(P49_PD_PTE_ADDRESS);barrier();return p49_pd_parameter_uncached(pte)&&pte==*ptr(P49_PD_PTE_ADDRESS)&&*ptr(0x813ebf50u)==P48_CC_FIXED_BASE0&&*ptr(0x813ebf54u)==P48_CC_FIXED_BASE1;}
static int gt_equal(uint32_t at,const volatile uint32_t *p,unsigned n){for(unsigned i=0;i<n;i++)if(*ptr(at+4*i)!=p[i])return 0;return 1;}
static uint32_t gt_hash(uint32_t at){uint32_t h=2166136261u;for(unsigned i=0;i<GT_WORDS;i++){uint32_t v=*ptr(at+4*i);for(unsigned j=0;j<4;j++){h=(h^(v&255u))*16777619u;v>>=8;}}return h;}
static GT_FN int gt_prepare(void){
 volatile uint32_t*q=gt();uint32_t request[64];for(unsigned i=0;i<64;i++)request[i]=q[i];
 if(!gt_request_valid(request,ctx.nonce,cc()[63])||request[7]!=wb()[47]||!cc_configured()||cc()[71]||!gt_roots())return 0;
 for(unsigned i=64;i<GT_BYTES/4;i++)q[i]=0;
 for(unsigned i=0;i<GT_WORDS;i++)q[GT_SAVED+i]=*ptr(GT_DDR+4*i);
 if(!gt_equal(GT_DDR,q+GT_SAVED,GT_WORDS))return 0;
 q[GT_BANK0_HASH]=gt_hash(GT_DDR0);q[GT_READY]=1;barrier();return 1;
}
static GT_FN int gt_apply(void){
 volatile uint32_t*q=gt();if(!q[GT_READY]||q[GT_CALLED]||cc()[71]||!gt_roots())return 0;
 if(!q[3]){q[GT_BANK0_SAME]=1;return 1;}
 uint32_t obj=ctx.cap+GT_OFFSET+0x1000u,metadata=obj+0x1000u,widths=metadata+0x100u,parent=metadata+0x200u;
 volatile uint32_t *o=ptr(obj);o[0]=0x8062f6b0u;o[2]=metadata;o[3]=parent;o[6]=widths;o[7]=5;
 *ptr(metadata)=2;*ptr(parent+12)=256;const uint32_t sizes[5]={3,5,4,13,2};for(unsigned i=0;i<5;i++)ptr(widths)[i]=sizes[i];
 for(unsigned i=0;i<19;i++){ptr(obj+0xedcu)[i]=q[16+i];ptr(obj+0xf28u)[i]=gt_y(q[3],q[35+i]);}
 q[GT_CALLED]=1;barrier();q[GT_STATUS]=(uint32_t)native(GT_CALCULATOR,obj,0xaa000000u,0);
 if(q[GT_STATUS]!=1)return 0;
 for(unsigned i=0;i<19;i++)if(ptr(obj+0xe40u)[i]!=q[16+i]||ptr(obj+0xe8cu)[i]!=gt_y(q[3],q[35+i]))return 0;
 /* Preserve original padding bits; masks are checked against the actual
  * original BFI packing and four independent stack seeds on PC. */
 for(unsigned i=0;i<GT_WORDS;i++){
  unsigned j=i%54u;uint32_t mask=j<11u?0x7fff7fffu:j<22u?0x07ff07ffu:j==53u?0x0000ffffu:0xffffffffu;
  q[GT_TARGET+i]=(*ptr(GT_DDR+4*i)&mask)|(q[GT_SAVED+i]&~mask);
 }
 if(native(GT_WRITER,1,(uint32_t)(uintptr_t)(q+GT_TARGET),0))return 0;
 q[GT_TARGET_MATCH]=gt_equal(GT_DDR,q+GT_TARGET,GT_WORDS);q[GT_BANK0_SAME]=gt_hash(GT_DDR0)==q[GT_BANK0_HASH];barrier();
 return q[GT_TARGET_MATCH]&&q[GT_BANK0_SAME];
}
static GT_FN int gt_restore(void){
 volatile uint32_t*q=gt();if(!q[GT_CALLED])return 1;if(q[GT_RESTORE_CALLED])return !q[GT_RESTORE_STATUS]&&q[GT_RESTORED];
 if(cc()[71]||wb()[71]||!q[GT_READY]||!gt_roots())return 0;
 q[GT_RESTORE_CALLED]=1;barrier();q[GT_RESTORE_STATUS]=(uint32_t)native(GT_WRITER,1,(uint32_t)(uintptr_t)(q+GT_SAVED),0);
 q[GT_RESTORED]=gt_equal(GT_DDR,q+GT_SAVED,GT_WORDS);q[GT_RESTORE_BANK0_SAME]=gt_hash(GT_DDR0)==q[GT_BANK0_HASH];barrier();return !q[GT_RESTORE_STATUS]&&q[GT_RESTORED];
}
static GT_FN void gt_observe(unsigned word){if(gt()[3])gt()[word]=gt_equal(GT_DDR,gt()+GT_TARGET,GT_WORDS);barrier();}
