/* SPDX-License-Identifier: AGPL-3.0-only. Fixed-size view of the leased TMC RAM.
 * The RTOS producer cleans each completed record to physical RAM. No target
 * writes, calls, queues or per-frame polling occur on the Linux reader. */
#ifndef NXL_VIDEO_LATENCY_CAPTURE_H
#define NXL_VIDEO_LATENCY_CAPTURE_H
#define VP_LATENCY 65536u
#define VP_LATENCY_DATA 0x817dbbe0u
#define VP_LATENCY_BYTES 256u
#define VP_LATENCY_MAGIC 0x33544c56u
static int vp_latency_capture(const struct vp_reader*r,struct vp_record*s){
 uint32_t a[64],b[8];memset(s->latency,0,sizeof s->latency);
 if(!r->physical(r->context,VP_LATENCY_DATA,a,sizeof a)||!r->physical(r->context,VP_LATENCY_DATA,b,sizeof b)||memcmp(a,b,sizeof b)||a[0]!=VP_LATENCY_MAGIC||a[1]!=4u)return 0;
 /* Sequence is published after the ring entry. An in-flight slot is zero
  * and is discarded by the decoder; sequence gaps remain explicit. */
 memcpy(s->latency,a,sizeof a);s->valid|=VP_LATENCY;return 1;
}
#endif
