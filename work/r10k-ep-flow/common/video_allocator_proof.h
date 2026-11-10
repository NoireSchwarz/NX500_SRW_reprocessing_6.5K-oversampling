/* SPDX-License-Identifier: AGPL-3.0-only. Video-only allocator observation.
 * Field UHD and original QHD both leave the OEM free-count above 45056.
 * Never use that aggregate as capacity or repair it. Prove the complete
 * block partition and the requested bank's contiguous extent instead.
 */
#ifndef NXL_VIDEO_ALLOCATOR_PROOF_H
#define NXL_VIDEO_ALLOCATOR_PROOF_H
#include <stdint.h>
#include <string.h>
struct vfa_head {uint32_t bank2,upper,reported_free;};
static int vfa_partition(const uint32_t*t,unsigned blocks,unsigned*free_blocks){
 if(!t||!free_blocks||blocks!=45056u)return 0;unsigned free_seen=0;
 for(unsigned i=0;i<blocks;){
  unsigned kind=t[4*i]&255u,span=t[4*i+1];
  if(!kind){if(span)return 0;free_seen++;i++;continue;}
  if(!span||span>blocks-i)return 0;
  for(unsigned j=1;j<span;j++){
   unsigned k=t[4*(i+j)]&255u,n=t[4*(i+j)+1];
   if(k?(k!=kind||n!=span-j):n!=0)return 0;
  }
  i+=span;
 }
 *free_blocks=free_seen;return 1;
}
#endif
