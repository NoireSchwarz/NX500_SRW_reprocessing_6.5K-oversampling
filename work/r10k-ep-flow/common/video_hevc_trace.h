/* SPDX-License-Identifier: AGPL-3.0-only. Linux-domain diagnostic ABI.
 * Hardware execution breakpoints; no app instructions or IRQ routes changed.
 * Trap scheduling/stepping perturb timing. On/off windows are compulsory.
 */
#ifndef NXL_VIDEO_HEVC_TRACE_H
#define NXL_VIDEO_HEVC_TRACE_H
#define HT_CAPACITY 5000u
#define HT_OFFSET VF_END
#define HT_MAGIC 0x31544248u
#define HT_XOR 0x428a9c31u
struct ht_header {uint32_t w[32];};
struct ht_record {
 uint64_t ns;uint32_t sequence,kind,request,y,uv,descriptor,object,status,pc,flags;
 uint64_t resume_ns;uint32_t reserved,commit;
};
#define HT_DATA (HT_OFFSET+sizeof(struct ht_header))
#define HT_END (HT_DATA+HT_CAPACITY*sizeof(struct ht_record))
enum {HT_SUBMIT=1,HT_WAIT_RETURN=2,HT_RESULT_RETURN=3,HT_RELEASE_ENTER=4,HT_RELEASE_RETURN=5,HT_WINDOW_ON=6,HT_WINDOW_OFF=7};
_Static_assert(sizeof(struct ht_record)==64,"HEVC event with observer cost");
#endif
