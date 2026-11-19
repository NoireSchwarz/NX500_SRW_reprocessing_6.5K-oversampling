/* SPDX-License-Identifier: AGPL-3.0-only. R11f narrow Linux HEVC admission gate.
 * No RTOS/MMIO/clock/QoS access. Disabled by default; a live short lease is required.
 */
#ifndef NXL_VIDEO_HEVC_GATE_H
#define NXL_VIDEO_HEVC_GATE_H
#include <stdint.h>
#define HG_MAGIC 0x31474148u
#define HG_SCHEMA 1u
#define HG_CODE 0x60000u
#define HG_STATE 0x68000u
#define HG_COMMAND 0x100u
#define HG_STATS 0x200u
#define HG_CALL 0x38530u
#define HG_ORIGINAL 0x3749cu
#define HG_PHASE_MS 30000u
#define HG_WARM_MS 10000u
#define HG_LEASE_MS 1200u
#define HG_STATS_WORDS 48u
struct hg_command {uint32_t seq,schema,cookie,enabled,encoder,queue,pid,ticks_lo,ticks_hi,start_ms,a_end_ms,b_end_ms,end_ms,lease_ms,reserved,hash;};
struct hg_stats {uint32_t seq,cookie,encoder,queue,calls,bad_command,expired,bad_guard,clock_failed,phase,last_ms,last_flags,cursor,inactive,first_ms[3],last_phase_ms[3],seen[3],passed[3],dropped[3],control[3],reserved[16];};
_Static_assert(sizeof(struct hg_command)==64,"command wire ABI");
_Static_assert(sizeof(struct hg_stats)==HG_STATS_WORDS*4,"stats wire ABI");
static uint32_t hg_hash(const uint32_t*p,unsigned n){uint32_t v=2166136261u;for(unsigned i=0;i<n;i++)v=(v^p[i])*16777619u;return v;}
static int hg_time_ge(uint32_t a,uint32_t b){return (int32_t)(a-b)>=0;}
#endif
