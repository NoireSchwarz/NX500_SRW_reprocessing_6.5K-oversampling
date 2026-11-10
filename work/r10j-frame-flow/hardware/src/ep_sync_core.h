/* SPDX-License-Identifier: AGPL-3.0-only */
#ifndef NXL_EP_SYNC_CORE_H
#define NXL_EP_SYNC_CORE_H
#include <stdint.h>
#include <string.h>
#define EP_KERNEL_DELTA 0x3a000000u
#define EP_KERNEL_STATE_VA 0xc07551c4u
#define EP_KERNEL_STATE_BYTES 0x554u
#define EP_KERNEL_MUTEX_VA 0xc071d81cu
#define EP_KERNEL_ACC_ARRAY_VA 0xc07554c8u
#define EP_KERNEL_ACC_MASK_VA 0xc0755238u
#define EP_SYNC_SELECTOR 13u
#define EP_SYNC_SAMPLES 16u
#define EP_DEVICE_FD_DATA 0x5556cu
#define EP_REGISTER_POINTERS_DATA 0x57720u
struct ep_sync_observation {
 uint32_t g_ep,acc_mask,completion[3],mutex[3];
 uint32_t top_pa,top_bytes,bitb_pa,bitb_bytes;
};
static uint32_t ep_sync_word(const unsigned char *p){uint32_t v;memcpy(&v,p,4);return v;}
static void ep_sync_decode(const unsigned char raw[EP_KERNEL_STATE_BYTES],const uint32_t mutex[3],struct ep_sync_observation *o){
 memset(o,0,sizeof *o);o->g_ep=ep_sync_word(raw);
 o->acc_mask=ep_sync_word(raw+EP_KERNEL_ACC_MASK_VA-EP_KERNEL_STATE_VA);
 unsigned offset=EP_KERNEL_ACC_ARRAY_VA+EP_SYNC_SELECTOR*12u-EP_KERNEL_STATE_VA;
 for(unsigned i=0;i<3;i++){o->completion[i]=ep_sync_word(raw+offset+i*4);o->mutex[i]=mutex[i];}
 /* reg_info: top, ldc, mc, rsz, lvr, bblt, fd, jpeg, 3dlut, nog. */
 o->top_pa=ep_sync_word(raw+4);o->top_bytes=ep_sync_word(raw+8);
 o->bitb_pa=ep_sync_word(raw+4+5*8);o->bitb_bytes=ep_sync_word(raw+8+5*8);
}
static const char *ep_sync_observation_error(const struct ep_sync_observation *o){
 const uint32_t list=EP_KERNEL_ACC_ARRAY_VA+EP_SYNC_SELECTOR*12u+4u;
 if(o->g_ep<0xc0008000u||o->g_ep>=0xe0000000u||(o->g_ep&3))return "EP_KERNEL_DEVICE_CONTEXT_UNCONFIRMED";
 if(o->top_pa!=0x20820000u||o->top_bytes!=0x1c00u||o->bitb_pa!=0x20828000u||o->bitb_bytes!=0x1000u)return "EP_KERNEL_REGISTER_LAYOUT_UNCONFIRMED";
 if(o->mutex[0]!=1u||o->mutex[1]!=EP_KERNEL_MUTEX_VA+4||o->mutex[2]!=EP_KERNEL_MUTEX_VA+4)return "EP_KERNEL_MUTEX_NOT_OBSERVED_IDLE";
 if(o->acc_mask&(1u<<EP_SYNC_SELECTOR))return "EP_ACC13_INTERRUPT_ALREADY_ENABLED";
 if(o->completion[1]!=list||o->completion[2]!=list)return "EP_ACC13_WAITERS_OR_LAYOUT_UNCONFIRMED";
 if(o->completion[0])return "EP_ACC13_STALE_COMPLETION_PRESENT";
 return NULL;
}
/* Metadata snapshots never acquire ownership or prove absence of future jobs. */
#endif
