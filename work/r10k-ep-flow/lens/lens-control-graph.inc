/* SPDX-License-Identifier: AGPL-3.0-only. Typed retained LDC metadata only.
 * Includes both controller objects and both 27-word config records even when
 * the active EP request has disappeared. Never follows image/DMA pointers.
 */
struct lsg_sample {
 uint32_t schema,status,read_mask,guards_before[3],guards_after[3];
 uint32_t controls[2][8],owners_before[2],owners_after[2],config[2][27];
};
static uint64_t graph_samples,graph_records,graph_failures,graph_changes,graph_at;
static struct lsg_sample graph_previous;
static int lens_graph_read(struct lsg_sample*s){
 memset(s,0,sizeof *s);s->schema=1;
 if(!block(LPR_CTRL0,s->guards_before,1)||!block(LPR_CTRL1,s->guards_before+1,1)||!block(LPR_INPUT,s->guards_before+2,1)){s->status=3;return -1;}
 if(s->guards_before[0]!=0x80582d10u||s->guards_before[1]!=0x80582da8u||s->guards_before[2]!=0x80713a18u){s->status=2;return 1;}
 if(!block(LPR_CTRL0,s->controls[0],8)||!block(LPR_CTRL1,s->controls[1],8)||!block(LPR_STORAGE+544,s->owners_before,2)){s->status=3;return -1;}
 for(unsigned i=0;i<2;i++)if(s->owners_before[i]){
  if(!lf_ram(s->owners_before[i],108)){s->status=5;return 1;}
  if(!block(s->owners_before[i],s->config[i],27)){s->status=3;return -1;}s->read_mask|=1u<<i;
 }
 if(!block(LPR_STORAGE+544,s->owners_after,2)||!block(LPR_CTRL0,s->guards_after,1)||!block(LPR_CTRL1,s->guards_after+1,1)||!block(LPR_INPUT,s->guards_after+2,1)){s->status=3;return -1;}
 if(memcmp(s->owners_before,s->owners_after,8)||memcmp(s->guards_before,s->guards_after,12)){s->status=4;return 0;}
 s->status=1;return 1;
}
static void lens_graph_sample(unsigned sequence){
 uint64_t begin=now();struct lsg_sample s;int rc=lens_graph_read(&s);graph_samples++;
 if(rc<0)graph_failures++;if(!rc)graph_changes++;
 if(!graph_at||memcmp(&s,&graph_previous,sizeof s)||begin-graph_at>=1000000000ULL){
  if(frame(context_file,11,sequence,begin,now(),&s,sizeof s))graph_records++;
  graph_previous=s;graph_at=begin;
 }
}
static void lens_graph_summary(void){
 fprintf(events,"{\"event\":\"lens_retained_control_graph\",\"samples\":%llu,\"records\":%llu,\"read_failures\":%llu,\"guard_changes\":%llu,\"atomic\":false,\"image_pointer_following\":false}\n",(unsigned long long)graph_samples,(unsigned long long)graph_records,(unsigned long long)graph_failures,(unsigned long long)graph_changes);
}
