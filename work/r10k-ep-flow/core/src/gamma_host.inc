#include "gamma_contract.h"
static uint32_t gt_copy[GT_AUDIT_BYTES/4];
static unsigned source_iq_evidence_saved;
static int gt_host_prepare(uint32_t nonce){
 const char*mode=getenv("NX51_GAMMA_MODE");unsigned m=1;if(mode){if(mode[0]<'0'||mode[0]>'2'||mode[1])return 0;m=(unsigned)(mode[0]-'0');}
 const unsigned char*c=p48_source->c0;if(!c)return 0;
 uint32_t q[64]={GT_MAGIC,1,nonce,m,((const uint32_t*)(expected+P48_CC_OFFSET))[63],1,1};
 q[7]=p39_fnv(c,2048);
 for(unsigned i=0;i<19;i++){q[16+i]=p41_u32(c+356+4*i,1);q[35+i]=p41_u32(c+432+4*i,1);}
 q[63]=p48_batch_crc(q,252);if(!gt_request_valid(q,nonce,q[4]))return 0;
 memset((void*)(cap_alias+GT_OFFSET),0,GT_BYTES);memcpy((void*)(cap_alias+GT_OFFSET),q,sizeof q);__sync_synchronize();
 return !memcmp((const void*)(cap_alias+GT_OFFSET),q,sizeof q);
}
