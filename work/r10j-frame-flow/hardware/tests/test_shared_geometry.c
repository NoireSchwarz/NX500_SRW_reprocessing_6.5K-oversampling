#define _GNU_SOURCE
#include <stdio.h>
#include "../src/bitb_job.h"
#include "../src/p11_engine.h"
int main(void){uint32_t p[26];printf("{\"width\":%u,\"height\":%u,\"capacity\":%u,\"packets\":[",TEST_W,TEST_H,TEST_BYTES);
 for(unsigned write=0;write<2;write++){p11_dma(p,0x90000000u+(write?TEST_OUT:TEST_IN),write);printf("%s[",write?",":"");for(unsigned i=0;i<26;i++)printf("%s%u",i?",":"",p[i]);printf("]");}puts("]}");return 0;}
