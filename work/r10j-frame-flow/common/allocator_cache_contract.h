/* Native metadata cache publication, never a release receipt by itself. */
#ifndef NXL_ALLOCATOR_CACHE_CONTRACT_H
#define NXL_ALLOCATOR_CACHE_CONTRACT_H
#include <stdint.h>
#define AC_NATIVE_SEND 0x83ea4u
#define AC_ATTRIBUTE 0x18fu
#define AC_START 0x81dfc800u
#define AC_BYTES 0xb0600u
#define AC_SEND_WAIT_MS 2000u
#define AC_VERIFY_WAIT_MS 3000u
static const uint32_t ac_send_words[8]={0xe92d4800u,0xe28db004u,0xe24dd010u,0xe50b0008u,0xe50b3010u,0xe14b10bau,0xe14b20bcu,0xe15b20bau};
struct ac_code_guard {uint32_t address,bytes,hash;};
static const struct ac_code_guard ac_guards[]={
{0x800d27c0u,0x50u,0xe6e9b452u},
{0x8008fd40u,0x24u,0xc217e688u},
{0x80090afcu,0x5cu,0xa3ac7869u},
{0x804d981cu,0x24u,0x90f369c9u},
{0x8004d4acu,0x14u,0xb40ede6u},
{0x80574be8u,0x8u,0xd2846fd5u},
{0x80072cbcu,0x20u,0xda06e0a0u},
};
static int ac_payload_valid(const uint32_t p[3]){return p&&p[0]==0&&p[1]==AC_START&&p[2]==AC_BYTES&&!(p[1]&63u)&&!(p[2]&63u)&&(uint64_t)p[1]+p[2]<=0x94000000ULL;}
#endif
