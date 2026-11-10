/* Dedicated compile/link-boundary probe. No camera access or entry hook. */
#include "wb_contract.h"
int p49_arm_packet(uint32_t out[6],const uint32_t original[6],uint32_t word,uint32_t code){return p49_wb_make_packet(out,original,word,code);}
int p49_arm_request(const uint32_t v[256],uint32_t nonce){return p41_request_valid(v,nonce);}
