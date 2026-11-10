/* SPDX-License-Identifier: AGPL-3.0-only.
 * Exact FNV-1a over owned Normal-NC RAM. Never use for MMIO registers.
 * The volatile aligned word is folded as four bytes in original LE order;
 * prefix/tail byte reads preserve the requested extent without overread.
 */
#ifndef NXL_FNV_UNCACHED_H
#define NXL_FNV_UNCACHED_H
#include <stdint.h>
#if !defined(__BYTE_ORDER__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "Owned CAP FNV word reader requires a little-endian target"
#endif
typedef uint32_t nxl_fnv_alias_word __attribute__((__may_alias__));
static inline uint32_t nxl_fnv1a_volatile(const volatile unsigned char *p,uint32_t n){
 uint32_t h=2166136261u;
 while(n&&((uintptr_t)p&3u)){h=(h^*p++)*16777619u;n--;}
 while(n>=4u){
  uint32_t w=*(const volatile nxl_fnv_alias_word*)p;
  h=(h^(w&255u))*16777619u;
  h=(h^((w>>8)&255u))*16777619u;
  h=(h^((w>>16)&255u))*16777619u;
  h=(h^(w>>24))*16777619u;
  p+=4;n-=4;
 }
 while(n--){h=(h^*p++)*16777619u;}
 return h;
}
#endif
