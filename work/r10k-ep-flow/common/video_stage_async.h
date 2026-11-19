/* SPDX-License-Identifier: AGPL-3.0-only. R11a cold maps, hot vectors.
 * A full scan brackets a window over its actual saved time interval; it
 * is never represented as an instantaneous map or a hardware latch. */
#ifndef NXL_VIDEO_STAGE_ASYNC_H
#define NXL_VIDEO_STAGE_ASYNC_H
#include "video_tail_capture.h"
#define VZ_SCAN 8u
static void vz_async_init(struct bv_header*h,struct vz_state*v){(void)v;h->w[1]=5u;}
static void vz_scan_record(struct bv_header*h,struct bv_record*records,struct vz_state*v,
 uint64_t stamp,unsigned disposition){
 struct bv_record z;memset(&z,0,sizeof z);z.kind=VZ_SCAN;z.window=v->cold_state==3u?v->cold_window:0;
 z.begin_ns=z.end_ns=stamp;z.d[0]=disposition;z.d[1]=v->cold_state;z.d[2]=v->cold_map.status;
 z.d[3]=v->cold_map.lines;z.d[4]=v->cold_map.bytes;z.d[5]=v->cold_map.failure_stage;z.d[6]=v->cold_map.read_errno;
 z.d[7]=v->cold_map.pthread_seen;z.d[8]=v->cold_map.pthread_base;z.d[9]=v->cold_map.count;
 z.d[10]=(uint32_t)v->cold_map.scan_begin_ns;z.d[11]=(uint32_t)(v->cold_map.scan_begin_ns>>32);
 z.d[12]=(uint32_t)v->cold_map.scan_end_ns;z.d[13]=(uint32_t)(v->cold_map.scan_end_ns>>32);
 z.d[14]=v->cold_map.epoch;z.d[15]=(uint32_t)v->metadata_cpu;z.d[16]=(uint32_t)(v->metadata_cpu>>32);
 bv_append(h,records,&z);v->cold_diag_due=stamp+1000000000ULL;
}
static void vz_async_map(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,const struct vz_io*io,
 struct vz_state*v,uint64_t(*now)(void*),void*c,unsigned side,int external_reads){
 struct bv_record z;memset(&z,0,sizeof z);z.kind=BV_MAP;z.window=v->cold_window;z.flags=side;z.begin_ns=now(c);
 const struct vz_maps*m=&v->cold_map;z.d[0]=m->count;z.d[1]=m->status;z.d[2]=m->full_hash;memcpy(z.d+4,m->map,sizeof m->map);
 z.d[36]=v->selected;z.d[37]=(uint32_t)v->selected_ticks;z.d[38]=(uint32_t)(v->selected_ticks>>32);
 z.d[39]=external_reads&&v->selected&&io->select?io->select(io->context,v->selected,v->selected_ticks):0;
 z.d[46]=m->pthread_layout_verified;z.d[47]=m->pthread_base;
 if(external_reads){static const uint32_t slots[5]={0x817d8adcu,0x8153cb84u,0x817d8afcu,0x8153cb48u,0x817d8aacu};
  for(unsigned i=0;i<5;i++)if(vp_rw(r,slots[i],z.d+40+i))z.d[45]|=1u<<i;
 }
 z.d[48]=(uint32_t)m->scan_begin_ns;z.d[49]=(uint32_t)(m->scan_begin_ns>>32);
 z.d[50]=(uint32_t)m->scan_end_ns;z.d[51]=(uint32_t)(m->scan_end_ns>>32);
 z.d[52]=m->lines;z.d[53]=m->bytes;z.d[54]=m->failure_stage;z.d[55]=m->read_errno;
 z.d[56]=m->status==1u;z.d[57]=m->epoch;z.d[58]=m->pthread_seen;
 if(v->selected&&!z.d[39]){v->selected=0;v->selected_ticks=0;io->select(io->context,0,0);}
 z.end_ns=now(c);bv_append(h,records,&z);
}
static int vz_cold_step(struct bv_header*h,struct bv_record*records,struct vz_state*v,const struct vz_io*io,
 uint64_t(*now)(void*),uint64_t(*cpu)(void*),void*c){
 uint64_t before=cpu(c);int status=io->maps_step(io->context,v->cold_reset?1:0,&v->cold_map);v->cold_reset=0;
 v->metadata_cpu+=vo_cost(before,cpu(c));
 if(status||now(c)>=v->cold_diag_due)vz_scan_record(h,records,v,now(c),status>0?2u:status<0?3u:1u);
 return status;
}
/* Safe even after USB/stop: close our proc fd and append RAM-only failure
 * context. Never start target or filesystem reads while shutting down. */
static void vz_async_finish(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,
 struct vz_state*v,const struct vz_io*io,uint64_t(*now)(void*),void*c,unsigned reason){
 if(!v->cold_state)return;
 if(v->cold_state==1u||v->cold_state==3u){struct vz_maps m;io->maps_step(io->context,-1,&m);v->cold_map=m;}
 v->cold_map.status=2;v->cold_map.failure_stage=reason;v->cold_map.scan_end_ns=now(c);
 vz_scan_record(h,records,v,now(c),4u);
 if(v->cold_state==3u)vz_async_map(r,h,records,io,v,now,c,2u,0);
 v->cold_state=0;v->cold_reset=0;v->cold_legacy=0;
}
static void vz_async_tick(const struct vp_reader*r,struct bv_header*h,struct bv_record*records,struct bv_state*b,
 struct vz_state*v,const struct vz_io*io,uint64_t(*now)(void*),uint64_t(*cpu)(void*),void(*wait)(void*),void*c,uint64_t deadline){
 uint64_t stamp=now(c);if(deadline<=stamp+8000000u)return;
 if(memcmp(v->context_key,b->key,32)){
  vz_async_finish(r,h,records,v,io,now,c,9u);memcpy(v->context_key,b->key,32);
  v->task_inventory_hash=0;memset(v->task_rejection,0,sizeof v->task_rejection);v->selected=0;v->selected_ticks=0;v->ntids=v->task_at=0;v->scan_due=stamp;b->next_window=stamp;
  v->header_seen=0;v->header_progress_ns=0;io->select(io->context,0,0);
 }
 if(v->cold_state==3u){
  /* One optional old-Que followup on a later100ms tick, before the after
   * scan starts. It does not consume the first-vector hot budget. */
  if(v->cold_legacy&&deadline>now(c)+15000000u){
   uint32_t head[16];int progressing=pl_read_header(r,head)&&vp_timer_schema(head)==11u&&vp_timer_inflight(head)&&(head[3]&512u)&&
    (head[2]!=v->last_header_sequence||head[5]!=v->last_header_tick);
   if(progressing&&b->phase==2u&&b->watch_valid)bv_light(r,h,records,b,now,c,0);
   b->watch_valid=0;v->cold_legacy=0;return;
  }
  if(vz_cold_step(h,records,v,io,now,cpu,c)){
   uint64_t cost=cpu(c);vz_async_map(r,h,records,io,v,now,c,2u,1);v->metadata_cpu+=vo_cost(cost,cpu(c));v->cold_state=0;
  }return;
 }
 if((b->phase!=1u&&b->phase!=2u)||!b->source||stamp-b->changed_ns<1000000000ULL)return;
 unsigned preview=b->phase==1u?(b->recording_ns?2u:1u):0;
 if(preview&&(v->preview_mask&preview))return;
 if(h->w[4]+80u>=BV_CAPACITY){h->w[6]|=1u;return;}
 uint64_t meta=cpu(c);vz_discover(r,h,records,io,v,b,now,c);v->metadata_cpu+=vo_cost(meta,cpu(c));
 if(v->cold_state==1u){if(vz_cold_step(h,records,v,io,now,cpu,c))v->cold_state=2u;return;}
 uint32_t hdr[16];if(!pl_read_header(r,hdr)||vp_timer_schema(hdr)!=11u||!vp_timer_inflight(hdr)||!(hdr[3]&512u)){v->header_seen=0;return;}
 if(!v->header_seen){v->header_seen=1;v->last_header_sequence=hdr[2];v->last_header_tick=hdr[5];return;}
 if(v->last_header_sequence!=hdr[2]||v->last_header_tick!=hdr[5]){v->header_progress_ns=stamp;v->last_header_sequence=hdr[2];v->last_header_tick=hdr[5];}
 if(!v->header_progress_ns||stamp-v->header_progress_ns>200000000u)return;
 if(!v->cold_state){
  if(now(c)<b->next_window)return;
  v->cold_state=1;v->cold_reset=1;v->cold_diag_due=0;(void)vz_cold_step(h,records,v,io,now,cpu,c);return;
 }
 if(v->cold_state!=2u||deadline<=now(c)+15000000u)return;
 /* All unbounded-size maps work has finished. First capture the actual
  * stage vector, before optional CImage/old Que work. */
 b->window=++h->w[5];v->cold_window=b->window;v->preview_mask|=preview;
 b->count=0;b->watch_valid=0;v->pending=0;b->next_vector=0;
 meta=cpu(c);vz_async_map(r,h,records,io,v,now,c,1u,1);v->metadata_cpu+=vo_cost(meta,cpu(c));
 b->window_begin=now(c);b->next_window=b->window_begin+(uint64_t)h->w[10]*1000000u;
 uint64_t hot_cpu=cpu(c);unsigned reason=3,target=b->phase==2u?32u:16u;uint32_t anchor=UINT32_MAX;
 while(now(c)+5000000u<deadline){
  if(b->count&&cpu(c)-hot_cpu>=12000000u){reason=2;h->w[6]|=2u;break;}
  if(now(c)-b->window_begin>=80000000u||b->count>=target){reason=b->count>=target?1u:3u;break;}
  if(now(c)<b->next_vector){wait(c);continue;}
  if(b->phase==2u&&b->count%4u&&!v->pending)vt_capture(r,h,records,b,io,now,c,anchor);
  else{anchor=h->w[4];vz_stage(r,h,records,b,v,io,now,c,v->pending);}
  b->count++;b->next_vector=now(c)+2000000u;
  if(b->count==1u&&b->phase==2u&&cpu(c)-hot_cpu<6000000u&&now(c)+10000000u<deadline)bv_light(r,h,records,b,now,c,0);
 }
 if(cpu(c)-hot_cpu<8000000u&&now(c)+10000000u<deadline){
  if(b->phase==2u)bv_light(r,h,records,b,now,c,0);
  if(cpu(c)-hot_cpu<10000000u&&now(c)+8000000u<deadline)bv_images(r,h,records,b,now,c);
 }
 b->window_cpu=cpu(c)-hot_cpu;if(b->window_cpu>=12000000u){reason=2;h->w[6]|=2u;}
 unsigned watched=b->watch_valid;bv_end_window(h,records,b,now(c),reason);b->watch_valid=watched;
 if(pl_read_header(r,hdr)){v->last_header_sequence=hdr[2];v->last_header_tick=hdr[5];}
 v->cold_legacy=watched;v->cold_state=3;v->cold_reset=1;v->cold_diag_due=0;
}
#endif
