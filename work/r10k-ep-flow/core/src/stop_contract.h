/* SPDX-License-Identifier: AGPL-3.0-only. Native eventflag entry contracts. */
#ifndef P39_STOP_CONTRACT_H
#define P39_STOP_CONTRACT_H
#define P39_STOP_HOLDER 0x8153c1b0u
#define P39_STOP_PATTERN 0xbaadf00du
#define P39_STOP_TIMEOUT 2000u
#define P48_INITIAL_STOP_OFFSET 0xb900u
#define P48_INITIAL_STOP_BYTES 256u
static const uint32_t p39_stop_addresses[14]={0x8142a6f0u,0x8142a780u,0x20810308u,0x20810310u,0x20810314u,0x20810380u,0x20810384u,0x20810388u,0x20810390u,0x20810408u,0x2081040cu,0x20810418u,0x2081045cu,0x20810468u};
static int p39_stop_ack_valid(const uint32_t *s,uint32_t nonce,uint32_t pa){
 if(s[0]!=P39_STOP_MAGIC||(s[1]!=1&&s[1]!=3)||s[2]!=nonce||(s[3]<5||s[3]>6)||s[4]!=P39_STOP_HOLDER||s[5]<0x81000000u||s[5]>=0x93fff000u||(s[5]&3u)||!s[6]||s[6]>65535||s[7]!=P39_STOP_PATTERN||s[8]!=2000||s[29]!=pa+P39_STOP_COMMAND_OFFSET||s[31]!=1)return 0;
 for(unsigned i=9;i<=14;i++)if(s[i])return 0;
 return !s[17]&&!s[21]&&!s[25]&&!(s[22]&P39_STOP_PATTERN)&&(s[26]&P39_STOP_PATTERN)==P39_STOP_PATTERN&&(s[28]&P39_STOP_PATTERN)==P39_STOP_PATTERN&&s[19]==0xdeadcafeu&&s[23]==0xdeadcafeu&&s[27]==0xdeadcafeu&&!s[30]&&((s[1]==1&&!s[46]&&!s[47]&&!s[62]&&!s[63])||(s[1]==3&&(s[46]&1u)&&s[47]<=1&&s[62]==(s[47]?0x8005b36cu:0x800ce7d4u)&&s[63]==1));
}
static int p39_stop_record_valid(const uint32_t *s,uint32_t nonce,uint32_t pa){return p39_stop_ack_valid(s,nonce,pa)&&s[3]==6&&s[15]==1;}
/* P48.1: the first native stop is retained, never fabricated anew. Subsequent
 * records describe two read-only event references and current idle checks.
 * 0xffffffff means NOT CALLED for clear/post/wait; it never means success. */
#ifndef P48_STOP_FN
#define P48_STOP_FN
#endif
static P48_STOP_FN uint32_t p48_stop_fingerprint(const uint32_t *a){
 const unsigned char *p=(const unsigned char*)a;uint32_t h=2166136261u;
 for(unsigned i=0;i<P48_INITIAL_STOP_BYTES;i++)h=(h^p[i])*16777619u;return h;
}
static P48_STOP_FN int p48_stop_record_valid(const uint32_t *s,const uint32_t *a,uint32_t nonce,uint32_t pa,unsigned seq){
 if(!s||!a||!seq||seq>10||!p39_stop_record_valid(a,nonce,pa))return 0;
 if(seq==1){for(unsigned i=0;i<64;i++)if(s[i]!=a[i])return 0;return p39_stop_record_valid(s,nonce,pa);}
 return s[0]==P39_STOP_MAGIC&&s[1]==2&&s[2]==nonce&&s[3]==7&&s[4]==P39_STOP_HOLDER&&
  s[5]==a[5]&&s[6]==a[6]&&s[7]==P39_STOP_PATTERN&&!s[8]&&!s[9]&&
  s[10]==UINT32_MAX&&s[11]==UINT32_MAX&&s[12]==UINT32_MAX&&s[13]==UINT32_MAX&&!s[14]&&s[15]==1&&
  !s[17]&&!s[25]&&(s[18]&P39_STOP_PATTERN)==P39_STOP_PATTERN&&(s[26]&P39_STOP_PATTERN)==P39_STOP_PATTERN&&
  s[19]==0xdeadcafeu&&s[21]==seq-1&&s[22]==1&&s[23]==0xdeadcafeu&&s[27]==0xdeadcafeu&&!s[28]&&
  s[29]==pa+P48_INITIAL_STOP_OFFSET&&s[30]==1&&s[31]==1&&s[46]==seq&&s[47]==p48_stop_fingerprint(a)&&!s[62]&&!s[63];
}
#endif
