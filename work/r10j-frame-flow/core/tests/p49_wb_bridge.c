#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include "srw_iq.h"
int p49_test_read(const char *path,uint32_t out[256]){FILE *f=fopen(path,"rb");if(!f)return 0;int ok=p41_srw_info(f,out);if(fclose(f))return 0;return ok;}
int p49_test_packet(uint32_t out[6],const uint32_t original[6],uint32_t word,uint32_t code){return p49_wb_make_packet(out,original,word,code);}
uint32_t p49_test_checksum(const uint32_t *v,unsigned words){return p41_checksum(v,words);}
int p49_test_request(const uint32_t *v,uint32_t nonce){return p41_request_valid(v,nonce);}
int p49_test_audit(const uint32_t *v,uint32_t nonce){return p41_audit_valid(v,nonce);}
