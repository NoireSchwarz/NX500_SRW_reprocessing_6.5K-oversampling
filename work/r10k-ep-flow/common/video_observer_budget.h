/* SPDX-License-Identifier: AGPL-3.0-only. Independent observer CPU ledgers.
 * A heavy-read backoff is never a producer/ring-full or coverage failure.
 */
#ifndef NXL_VIDEO_OBSERVER_BUDGET_H
#define NXL_VIDEO_OBSERVER_BUDGET_H
struct vo_budget {uint64_t due,wall,fast,heavy,dense,stage,memory,metadata;unsigned heavy_ms,checks,fast_over,heavy_over,dense_over,dense_ms,target_ms,stage_ms,stage_over,memory_over,metadata_over;};
static void vo_init(struct vo_budget*b,uint64_t stamp){memset(b,0,sizeof*b);b->wall=stamp;b->due=stamp+10000000000ULL;b->heavy_ms=200u;b->dense_ms=5000u;b->target_ms=1000u;b->stage_ms=5000u;}
static uint64_t vo_cost(uint64_t a,uint64_t b){return b>=a?b-a:0;}
static int vo_check(struct vo_budget*b,uint64_t stamp){
 if(stamp<b->due)return 0;
 uint64_t wall=vo_cost(b->wall,stamp);b->checks++;
 if(wall&&b->fast>wall/500u)b->fast_over++;
 if(wall&&b->heavy>wall*3u/1000u){b->heavy_over++;b->heavy_ms=b->heavy_ms<500u?500u:b->heavy_ms<1000u?1000u:b->heavy_ms<2000u?2000u:5000u;}
 if(wall&&b->dense>wall/1000u){b->dense_over++;b->dense_ms=b->dense_ms<10000u?10000u:20000u;b->target_ms=b->target_ms<2000u?2000u:5000u;}
 if(wall&&b->stage>wall/250u){b->stage_over++;b->stage_ms=b->stage_ms<10000u?10000u:20000u;}
 if(wall&&b->memory>wall/100u)b->memory_over++; /* cold work: report, never reduce stage coverage */
 if(wall&&b->metadata>wall/100u)b->metadata_over++; /* bounded proc preparation, accounted separately */
 b->wall=stamp;b->due=stamp+10000000000ULL;return 1;
}
static void vo_reset_costs(struct vo_budget*b){b->fast=b->heavy=b->dense=b->stage=b->memory=b->metadata=0;}
#endif
