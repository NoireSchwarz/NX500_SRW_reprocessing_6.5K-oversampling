/* SPDX-License-Identifier: AGPL-3.0-only. Exercise the actual durable writer. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <signal.h>
#define P39_TASK 0x8153e6d4u
static volatile sig_atomic_t stop_requested;
static uint64_t now_ms;
static unsigned reads;
static int bad_state;
static int ro_words(uint32_t at,uint32_t *out,unsigned words){
 reads++;
 for(unsigned i=0;i<words;i++){
  uint32_t address=at+i*4;uint32_t value=0;
  if(address==P39_TASK)value=0x80592640u;
  else if(address==P39_TASK+3364)value=bad_state?2:0x12340007u;
  else if(address==P39_TASK+3428)value=0x819f1000u;
  else if(address==0x819f1000u)value=0x805926c8u;
  else if(address==0x81543d40u)value=0x819f0000u;
  else if(address==0x819f0000u)value=0x12340001u;
  else if(address==0x8142d154u)value=0x80582820u;
  else if(address==0x8142d154u+3444)value=0x8142dee0u;
  out[i]=value;
 }
 return 1;
}
static uint64_t ns(void){return now_ms*1000000ULL;}
static void pause_ms(unsigned ms){now_ms+=ms;}
#include "front_readiness_host.inc"
int main(int argc,char **argv){
 if(argc!=3)return 2;
 bad_state=!strcmp(argv[2],"reject");
 int ready=front_initial_ready(argv[1]);
 if(ready==bad_state||!reads)return 1;
 /* Calls outside the initial handoff retain the pure predicate and do not
  * produce extra files or repeated report-write collisions. */
 if(front_idle()==bad_state)return 1;
 puts(ready?"PASS durable readiness accepted":"PASS durable readiness refusal");return 0;
}
