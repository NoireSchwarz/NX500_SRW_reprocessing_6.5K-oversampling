/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef P24_SHA256_H
#define P24_SHA256_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct sha256 {uint32_t h[8];uint64_t bytes;unsigned used;unsigned char block[64];};
static uint32_t rotr32(uint32_t x,unsigned n){return x>>n|x<<(32-n);}
static uint32_t be32(const unsigned char *b){return (uint32_t)b[0]<<24|(uint32_t)b[1]<<16|(uint32_t)b[2]<<8|b[3];}
static void sha256_block(struct sha256 *s,const unsigned char *b){
 static const uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 uint32_t w[64];for(unsigned i=0;i<16;i++)w[i]=be32(b+4*i);
 for(unsigned i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];w[i]=w[i-16]+(rotr32(x,7)^rotr32(x,18)^(x>>3))+w[i-7]+(rotr32(y,17)^rotr32(y,19)^(y>>10));}
 uint32_t a=s->h[0],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7],bb=s->h[1];
 for(unsigned i=0;i<64;i++){uint32_t t=h+(rotr32(e,6)^rotr32(e,11)^rotr32(e,25))+((e&f)^(~e&g))+k[i]+w[i];uint32_t u=(rotr32(a,2)^rotr32(a,13)^rotr32(a,22))+((a&bb)^(a&c)^(bb&c));h=g;g=f;f=e;e=d+t;d=c;c=bb;bb=a;a=t+u;}
 s->h[0]+=a;s->h[1]+=bb;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static void sha256_init(struct sha256 *s){static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memset(s,0,sizeof *s);memcpy(s->h,iv,sizeof iv);}
static void sha256_update(struct sha256 *s,const void *data,size_t n){const unsigned char *b=data;s->bytes+=n;while(n){unsigned take=64-s->used;if(take>n)take=(unsigned)n;memcpy(s->block+s->used,b,take);s->used+=take;b+=take;n-=take;if(s->used==64){sha256_block(s,s->block);s->used=0;}}}
static void sha256_final(struct sha256 *s,unsigned char out[32]){uint64_t bits=s->bytes*8;unsigned char pad[128]={0x80};unsigned n=s->used<56?56-s->used:120-s->used;sha256_update(s,pad,n);for(unsigned i=0;i<8;i++)pad[i]=(unsigned char)(bits>>(56-i*8));sha256_update(s,pad,8);for(unsigned i=0;i<32;i++)out[i]=(unsigned char)(s->h[i/4]>>(24-(i%4)*8));}
static void digest_hex(const unsigned char b[32],char out[65]){static const char h[]="0123456789abcdef";for(unsigned i=0;i<32;i++){out[i*2]=h[b[i]>>4];out[i*2+1]=h[b[i]&15];}out[64]=0;}
static void digest_bytes(const void *b,size_t n,unsigned char out[32]){struct sha256 s;sha256_init(&s);sha256_update(&s,b,n);sha256_final(&s,out);}
#endif
