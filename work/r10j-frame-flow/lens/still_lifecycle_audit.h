/* SPDX-License-Identifier: AGPL-3.0-only. R7g observations, no lease or reset. */
#ifndef NXL_STILL_LIFECYCLE_AUDIT_H
#define NXL_STILL_LIFECYCLE_AUDIT_H
#include <stdint.h>
#define SLA_OFFSET 0xbe00u
#define SLA_RECORDS 6u
#define SLA_WORDS 20u
#define SLA_BYTES (SLA_RECORDS*SLA_WORDS*4u)
#define SLA_MAGIC 0x31414c53u
#define SLA_REF_SEM 0x804d9ea0u
#define SLA_SEM_A 0x8153cbdcu
#define SLA_SEM_B 0x8153cbe0u
#define SLA_UNKNOWN 0xdeadbeefu
static inline uint32_t sla_checksum(const uint32_t *w){
 uint32_t h=2166136261u;
 for(unsigned i=1;i<18;i++)for(unsigned k=0;k<4;k++)h=(h^((w[i]>>(8*k))&255u))*16777619u;
 return h;
}
static inline int sla_record_valid(const uint32_t *w,uint32_t nonce,unsigned stage){
 return nonce&&stage>=1&&stage<=SLA_RECORDS&&w[0]==SLA_MAGIC&&w[1]==1&&w[2]==nonce&&w[3]==stage&&
  w[18]==sla_checksum(w)&&w[19]==(nonce^stage^w[18]^SLA_MAGIC);
}
static inline int sla_complete(const uint32_t *w,uint32_t nonce){
 for(unsigned i=0;i<SLA_RECORDS;i++)if(!sla_record_valid(w+i*SLA_WORDS,nonce,i+1))return 0;
 return 1;
}
_Static_assert(SLA_OFFSET>=0xbd00u&&SLA_OFFSET+SLA_BYTES<=0xc000u,"audit must stay in unused owned RAM");
#endif
