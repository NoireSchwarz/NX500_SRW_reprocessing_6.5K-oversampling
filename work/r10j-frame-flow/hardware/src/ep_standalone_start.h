/* SPDX-License-Identifier: AGPL-3.0-only. Linux libudd5 0x2e6f0 ABI.
 * These are module/channel ENUMERATIONS, not enable bit masks. The original
 * wrapper converts an RDMA channel to 1<<(16+channel), and WDMA to
 * 1<<(20+channel); UINT32_MAX entries disable unused channel slots.
 * Evidence: replay the unchanged native wrapper, including its PLT callees. */
#ifndef NXL_EP_STANDALONE_START_H
#define NXL_EP_STANDALONE_START_H
struct ep_standalone_start {uint32_t module,rdma[3],wdma[4],module_option;};
_Static_assert(sizeof(struct ep_standalone_start)==36,"original standalone start ABI");
_Static_assert(offsetof(struct ep_standalone_start,rdma)==4&&offsetof(struct ep_standalone_start,wdma)==16&&offsetof(struct ep_standalone_start,module_option)==32,"original start field offsets");
/* P7 Still ctor8011635c / run801161a8: CS1 option, no RDMA pulse. */
static void ep_standalone_ldc_still_start(struct ep_standalone_start*s){
 const struct ep_standalone_start ldc={0,{~0u,~0u,~0u},{0,~0u,~0u,~0u},1};*s=ldc;
}
/* The explicit original memory-to-memory View route uses this same9-word
 * argument ABI for d5_ep_top_start_auto, with CS0 and both real channels. */
static void ep_auto_ldc_m2m_start(struct ep_standalone_start*s){
 const struct ep_standalone_start ldc={0,{0,~0u,~0u},{0,~0u,~0u,~0u},0};*s=ldc;
}
/* Explicit single-memory submit uses the same argument layout, but the
 * original standalone function pulses module0+WDMA0 and then RDMA0 at
 * TOP1008. It does not arm a continuous/global OTF frame path. */
static void ep_single_ldc_m2m_start(struct ep_standalone_start*s){
 const struct ep_standalone_start ldc={0,{0,~0u,~0u},{0,~0u,~0u,~0u},0};*s=ldc;
}
#endif
