/* R11f controller: writes only the added 64-byte command mailbox.
 * Text, OEM objects, descriptors, buffers, RTOS, MMIO and selectors remain unwritten.
 */
struct hg_runtime {int fd;unsigned status,reason,phase,started,finalized,seq,log_failed,concurrency,foreign_calls,foreign_this;uint32_t cookie,base,key[8],live[8],cpu[2],ddr[2],hal_valid,hal_success,shape[4];uint64_t hal_begin_ns,hal_end_ns,start_ns,finish_ns,log_due,telemetry_ns;struct hg_command cmd;struct hg_stats stats;};
static int hg_snapshot(struct hg_runtime*g){
 struct hg_stats s;uint32_t seq=0,concurrency=0;
 if(pread(g->fd,&s,sizeof s,(off_t)((uint64_t)g->base+HG_STATE+HG_STATS))!=(ssize_t)sizeof s||
 pread(g->fd,&seq,4,(off_t)((uint64_t)g->base+HG_STATE+HG_STATS))!=4||s.seq!=seq||(seq&1u)||
 pread(g->fd,&concurrency,4,(off_t)((uint64_t)g->base+HG_STATE+132u))!=4)return 0;
 g->stats=s;g->concurrency=concurrency;
 uint32_t foreign[2]={0};if(pread(g->fd,foreign,8,(off_t)((uint64_t)g->base+HG_STATE+136u))==8){g->foreign_calls=foreign[0];g->foreign_this=foreign[1];}return 1;
}
static int hg_send(struct hg_runtime*g,unsigned enabled,uint64_t now){
 if(g->fd<0)return 0;
 struct hg_command q=g->cmd;g->seq+=2u;if(!g->seq)g->seq=2u;
 q.seq=g->seq;q.enabled=enabled;q.lease_ms=(uint32_t)(now/1000000u)+HG_LEASE_MS;
 q.hash=hg_hash((const uint32_t*)&q+1,14);uint32_t odd=q.seq|1u;
 off_t at=(off_t)((uint64_t)g->base+HG_STATE+HG_COMMAND);
 /* Every write is a fixed subrange of our newly added command, never a
  * caller-selected target address. A partial update stays odd and passes. */
 if(pwrite(g->fd,&odd,4,at)!=4||pwrite(g->fd,(const unsigned char*)&q+4,sizeof q-4,at+4)!=(ssize_t)sizeof q-4||pwrite(g->fd,&q.seq,4,at)!=4)return 0;
 g->cmd=q;return 1;
}
static void hg_publish(const struct hg_runtime*g,struct bv_header*h){
 unsigned phase=g->status==DM_DONE?3u:g->started?(g->phase?g->phase:1u):0;
 __atomic_store_n(h->w+20,DM_STATUS_TAG|g->status|(g->reason<<8)|(phase<<16),__ATOMIC_RELEASE);
}
static int hg_log(struct hg_runtime*g,struct bv_header*h,struct bv_record*r,uint64_t now,unsigned event){
 struct bv_record z;memset(&z,0,sizeof z);z.begin_ns=z.end_ns=now;z.kind=12u;
 uint32_t*w=z.d;w[0]=HG_MAGIC;w[1]=HG_SCHEMA;w[2]=event;w[3]=g->status;w[4]=g->reason;w[5]=g->phase;
 w[6]=g->cookie;w[7]=g->base;w[8]=(uint32_t)(g->start_ns/1000000u);w[9]=(uint32_t)(g->finish_ns/1000000u);
 w[10]=g->seq;w[11]=g->cmd.enabled;w[12]=g->cmd.lease_ms;w[13]=g->concurrency;w[14]=HG_PHASE_MS;w[15]=HG_LEASE_MS;
 w[16]=g->cpu[0];w[17]=g->cpu[1];w[18]=g->ddr[0];w[19]=g->ddr[1];w[20]=g->telemetry_ns&&now>=g->telemetry_ns?(uint32_t)((now-g->telemetry_ns)/1000000u):UINT32_MAX;
 memcpy(w+24,g->key,32);memcpy(w+32,g->live,32);memcpy(w+40,&g->cmd,64);memcpy(w+56,&g->stats,sizeof g->stats);
 w[104]=(uint32_t)g->start_ns;w[105]=(uint32_t)(g->start_ns>>32);w[106]=(uint32_t)g->finish_ns;w[107]=(uint32_t)(g->finish_ns>>32);w[108]=(uint32_t)g->telemetry_ns;w[109]=(uint32_t)(g->telemetry_ns>>32);
 w[110]=g->hal_valid;w[111]=g->hal_success;memcpy(w+112,g->shape,16);w[116]=(uint32_t)g->hal_begin_ns;w[117]=(uint32_t)(g->hal_begin_ns>>32);w[118]=(uint32_t)g->hal_end_ns;w[119]=(uint32_t)(g->hal_end_ns>>32);w[120]=g->key[1];w[121]=g->key[7];w[122]=g->key[3];w[123]=g->foreign_calls;w[124]=g->foreign_this;
 if(!bv_append(h,r,&z)){g->log_failed=1;return 0;}return 1;
}
static int hg_live(const struct vp_reader*r,const struct bv_state*b,struct hg_runtime*g){
 uint32_t owner=0,nodes[2]={0},q[3]={0};
 if(!bv_identity(r,g->live)||g->live[0]!=b->camera||g->live[2]!=b->source||g->live[4]!=2u||
 !vp_type(r,b->encoder,r->core+0x59da8u)||!vp_type(r,b->pool,r->core+0x593b0u)||
 !vp_pw(r,b->source+0x48u,&owner)||owner<4u||!vp_type(r,owner,r->core+0x5a11cu)||
 !vp_block(r,owner-4u+0x178u,nodes,8u)||(nodes[0]!=b->encoder&&nodes[1]!=b->encoder))return 0;
 vp_port_queues(r,b->encoder,q);return q[0]==b->pool;
}
static int hg_open(struct hg_runtime*g,struct runtime*c,uint32_t base,uint32_t cookie){
 memset(g,0,sizeof*g);g->fd=-1;g->status=DM_READY;g->base=base;g->cookie=cookie;
 uint32_t id[16],code[HG_IDENTITY_WORDS];
 if(!process(c,base+HG_STATE,id,sizeof id)||memcmp(id,hg_identity,sizeof id)||
 !process(c,base+HG_CODE,code,sizeof code)||hg_hash(code,HG_IDENTITY_WORDS)!=HG_IDENTITY_FNV){last_reason="TRACE_HEVC_GATE_RESTART_REQUIRED";return 0;}
 char path[80];snprintf(path,sizeof path,"/proc/%d/mem",c->pid);
 g->fd=open(path,O_RDWR|O_CLOEXEC);if(g->fd<0){last_reason="TRACE_HEVC_GATE_CONTROL_UNAVAILABLE";return 0;}
 g->cmd.schema=HG_SCHEMA;g->cmd.cookie=cookie;g->cmd.pid=(uint32_t)c->pid;g->cmd.ticks_lo=(uint32_t)c->start;g->cmd.ticks_hi=(uint32_t)(c->start>>32);
 if(!hg_send(g,0,clock_ns(CLOCK_MONOTONIC))){last_reason="TRACE_HEVC_GATE_CONTROL_FAILED";return 0;}
 return 1;
}
static void hg_end(struct hg_runtime*g,struct bv_header*h,struct bv_record*r,uint64_t now,unsigned why){
 if(g->finalized||g->fd<0)return;
 (void)hg_send(g,0,now);(void)hg_snapshot(g);g->finish_ns=now;
 if(g->status!=DM_DONE){g->status=DM_ABORTED;g->reason=why;}
 g->finalized=1;hg_publish(g,h);(void)hg_log(g,h,r,now,4u);
}
static void hg_close(struct hg_runtime*g){if(g->fd>=0){(void)hg_send(g,0,clock_ns(CLOCK_MONOTONIC));close(g->fd);g->fd=-1;}}
static void hg_tick(struct hg_runtime*g,const struct vp_reader*r,struct bv_header*h,struct bv_record*records,const struct bv_state*b,int native_ok,uint64_t now){
 if(g->status!=DM_READY&&g->status!=DM_RUNNING)return;
 if(!g->telemetry_ns||now-g->telemetry_ns>=1000000000u){cpu_report(g->cpu);ddr_report(g->ddr);
  uint32_t a[8],z[8],again[4];g->hal_valid=0;g->hal_begin_ns=clock_ns(CLOCK_MONOTONIC);
  if(b->phase==2u&&bv_identity(r,a)&&a[0]==b->camera&&a[2]==b->source&&a[4]==2u&&a[6]==b->key[7]&&a[7]==r->modules+0x4c9c8u&&
   vp_block(r,b->source+0x14cu,g->shape,8u)&&vp_block(r,a[6]+0x64u,g->shape+2,8u)&&vp_pw(r,a[6]+0x174u,&g->hal_success)&&
   vp_block(r,b->source+0x14cu,again,8u)&&vp_block(r,a[6]+0x64u,again+2,8u)&&!memcmp(again,g->shape,16u)&&bv_identity(r,z)&&!memcmp(a,z,32u))g->hal_valid=1u;
  g->hal_end_ns=clock_ns(CLOCK_MONOTONIC);g->telemetry_ns=clock_ns(CLOCK_MONOTONIC);now=clock_ns(CLOCK_MONOTONIC);}
 if(g->status==DM_READY){
  if(b->phase!=2u||!b->encoder||!b->pool||!b->changed_ns||now<b->changed_ns||now-b->changed_ns<(uint64_t)HG_WARM_MS*1000000u||!native_ok)return;
  if(!hg_live(r,b,g))return;
  if(h->w[4]+220u>=BV_CAPACITY){g->status=DM_UNAVAILABLE;g->reason=4u;hg_publish(g,h);(void)hg_log(g,h,records,now,4u);return;}
  memcpy(g->key,b->key,32);g->cmd.encoder=b->encoder;g->cmd.queue=b->pool;
  uint32_t ms=(uint32_t)(now/1000000u);g->cmd.start_ms=ms;g->cmd.a_end_ms=ms+HG_PHASE_MS;g->cmd.b_end_ms=ms+2u*HG_PHASE_MS;g->cmd.end_ms=ms+3u*HG_PHASE_MS;
  g->start_ns=now;g->started=1;g->status=DM_RUNNING;g->phase=1u;
 }
 if(b->phase!=2u||memcmp(b->key,g->key,32)||!native_ok||!hg_live(r,b,g)){hg_end(g,h,records,now,1u);return;}
 if(now<g->start_ns){hg_end(g,h,records,now,5u);return;}
 unsigned phase=(unsigned)((now-g->start_ns)/((uint64_t)HG_PHASE_MS*1000000u))+1u;
 if(phase>3u){
  (void)hg_send(g,0,now);(void)hg_snapshot(g);g->phase=3u;g->finish_ns=now;
  if(g->stats.cookie!=g->cookie||g->stats.bad_guard||g->stats.clock_failed||g->concurrency||
    g->stats.expired||g->stats.seen[0]<100u||g->stats.seen[1]<100u||g->stats.seen[2]<100u||
    !g->stats.dropped[1]||g->stats.dropped[0]||g->stats.dropped[2]){hg_end(g,h,records,now,7u);return;}
  g->status=DM_DONE;g->finalized=1;hg_publish(g,h);(void)hg_log(g,h,records,now,3u);return;
 }
 g->phase=phase;
 if(!hg_send(g,1,now)){hg_end(g,h,records,now,8u);return;}
 (void)hg_snapshot(g);
 if(now-g->start_ns>3000000000u&&(g->stats.cookie!=g->cookie||!g->stats.calls)){hg_end(g,h,records,now,7u);return;}
 if(g->stats.cookie==g->cookie&&(g->stats.bad_guard||g->stats.clock_failed||g->concurrency||g->stats.expired)){hg_end(g,h,records,now,7u);return;}
 if(!g->log_due||now>=g->log_due){g->log_due=now+500000000u;if(!hg_log(g,h,records,now,2u)){hg_end(g,h,records,now,4u);return;}}
 hg_publish(g,h);
}
