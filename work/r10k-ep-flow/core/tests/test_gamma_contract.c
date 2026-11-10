#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/gamma_contract.h"
#include "../src/param_dma_contract.h"
int main(void){
 uint32_t q[512]={GT_MAGIC,1,123,1,456,1,1};
 for(unsigned i=0;i<19;i++){q[16+i]=i==18?16383:i*800u;q[35+i]=i==18?1023:i*50u;}
 q[35]=0xfffffff6u;q[63]=p48_batch_crc(q,252);assert(gt_request_valid(q,123,456));assert(gt_y(2,q[35])==q[35]);
 q[GT_READY]=q[GT_CALLED]=q[GT_STATUS]=q[GT_TARGET_MATCH]=q[GT_RESTORE_CALLED]=q[GT_RESTORED]=q[GT_CONFIG_LOADS]=q[GT_BANK0_SAME]=q[GT_BEFORE_LOAD]=q[GT_CONFIG_RETURN]=1;
 q[GT_CONFIG_LOADS]=2;q[GT_REFRESH_CALLED]=q[GT_REFRESH_RETURN]=q[GT_FINAL_LOAD_CALLED]=1;
 assert(gt_audit_valid(q,123,456));/* post-start/complete mismatch stays observational */
 q[GT_AFTER_START]=q[GT_AFTER_COMPLETE]=1;assert(gt_audit_valid(q,123,456));
 q[GT_RESTORED]=0;assert(!gt_audit_valid(q,123,456));q[GT_RESTORED]=1;
 q[GT_CONFIG_LOADS]=1;assert(!gt_audit_valid(q,123,456));q[GT_CONFIG_LOADS]=2;
 q[GT_ERROR]=1;assert(!gt_audit_valid(q,123,456));q[GT_ERROR]=0;
 q[63]^=1;assert(!gt_request_valid(q,123,456));q[63]^=1;
 q[35]=0xfffffbffu;q[63]=p48_batch_crc(q,252);assert(!gt_request_valid(q,123,456));
 uint32_t d[64]={P49_PD_MAGIC,P49_PD_SCHEMA,123,8};d[48]=3;d[49]=d[53]=1;d[51]=2;assert(p49_pd_single_audit_valid(d,123));
 d[19]=1;assert(!p49_pd_single_audit_valid(d,123));d[19]=0;d[51]=1;assert(!p49_pd_single_audit_valid(d,123));
 puts("PASS Gamma signed nodes and the two original config loads; no synthetic completion");return 0;
}
