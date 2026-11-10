/* Real production state machine; IRQs and DMA bus acknowledgements are fixtures. */
#define _GNU_SOURCE
#define NXL_SMALL_MODEL
#define NXL_FAST_PIPELINE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/bitb_job.h"
#include "../src/ldc_engine.h"
static unsigned char memory[SW_BYTES];
static uint32_t top[2048],reg[1024],before_top[2048],before_reg[1024],pending[1024];
static unsigned mode,locked,armed,submitted,resets,configured,dirty,commit_top,commit_ldc,commit_barrier,rdma_updated,wdma_updated;
static unsigned checkpoints,coverage_calls,stops,requests,now,ack_reads,wait_calls,manual_calls,submit_pulses,dma_rearms;
static struct bitb_request q;
static int ctl(void*,uint32_t,void*);
static void checkpoint(void*u,unsigned phase){(void)u;if(phase==5){assert(checkpoints==4&&stops==1);checkpoints++;return;}assert(phase==checkpoints++);if(!phase)assert(!submitted);if(phase==2||phase==3)assert(submitted);}
static int wait_override(void*u,uint32_t*p){return mode==19?0:ctl(u,0x40086806,p);}
static int coverage(void*u){(void)u;assert(submitted&&armed&&stops&&requests);coverage_calls++;return mode!=18&&mode!=40;}
static uint32_t rd(void*u,unsigned b,uint32_t at){(void)u;
 if(!b&&at==0x124){
  if(mode==25&&stops)return 0x401;
  if(top[0x120/4]){ack_reads++;if(mode==24||mode==39)return 0;if(mode==26)return 1;if(mode==27)return ack_reads&1?0x401:1;return now>=2?0x401:0;}
  return 0;
 }return (b?reg:top)[at/4];
}
static void wr(void*u,unsigned b,uint32_t at,uint32_t v){(void)u;if(!b&&at==0x1050){top[0x10c8/4]&=~v;return;}
 if(!b&&at==0x120){if(v){assert(v==0x401&&stops&&armed);requests++;}else if(mode==30&&top[at/4])return;}
 (b?reg:top)[at/4]=v;
}
static int state(void*u,unsigned n){(void)u;return n?locked:!locked&&!armed;}
static int idle(void*u){(void)u;return mode!=2;}
static void barrier(void*u){(void)u;if(commit_ldc&&dirty&&mode!=11){memcpy(reg,pending,sizeof reg);dirty=0;commit_barrier=1;}}
static uint32_t ms(void*u){(void)u;return now;}
static void delay(void*u){(void)u;now++;}
static int ctl(void*u,uint32_t cmd,void*p){(void)u;(void)p;
 switch(cmd){case 0x6828:locked=1;return 0;case 0x6829:locked=0;return 0;case 0x40086807:return 0;case 0x4004680e:armed=1;return 0;
 case 0x4004680f:if(mode==8)return -1;armed=0;return 0;case 0x40086806:assert(manual_calls==3&&submit_pulses==2);wait_calls++;if(mode==16){memory[SW_LDC_OUT]^=1;return 0;}if(mode==6||mode==38)return 0;if(mode==7)top[0x10cc/4]=1;return 1;default:assert(0);return -1;}}
static int call(void*u,enum ldc_call fn,uintptr_t a,uint32_t b,uint32_t d){(void)u;
 switch(fn){
 case L_CLOCK:top[0x1020/4]&=~8u;top[0x1024/4]&=~8u;return 0;
 case L_RESET:assert(a==2);resets++;if(mode==35&&resets==2)return -1;memset(reg,0,sizeof reg);memset(pending,0,sizeof pending);dirty=0;return 0;
 case L_DMA_RESET:assert(locked&&!armed&&!submitted&&resets==1&&a==(dma_rearms?16u:0u));dma_rearms++;return(mode==44&&a==0)||(mode==45&&a==16)?-1:0;
 case L_SELECT:assert(a==0);top[0x1000/4]=mode==17?0x10:0;return 0;
 case L_UPDATE:assert(a==0||a==4||a==16||a==17);if(resets==1){if(a==16)commit_top=1;if(a==17){assert(commit_top);commit_ldc=1;}if(a==0)rdma_updated=1;if(a==4)wdma_updated=1;}return 0;
 case L_UPDATE_MODE:{assert(b==2&&!d&&(a==0||a==4||a==17));manual_calls++;if(mode==42)return -1;
  unsigned shift=(a<=15?a:a-7)*2;if(mode!=43)top[0x1048/4]=(top[0x1048/4]&~(3u<<shift))|(2u<<shift);return 0;}
 case L_CONFIG:{uint32_t*p=(uint32_t*)a,*t=(uint32_t*)(uintptr_t)p[23];assert(b==0&&p[10]==480&&p[11]==LDC_TILE_H&&!p[12]&&!p[13]);
  configured=dirty=1;pending[0x14/4]=mode==33?336:480;pending[0x18/4]=LDC_TILE_H;pending[0x74/4]=(t[1]&31u)|((t[0]&65535u)<<8);pending[0x78/4]=(t[2]&8191u)|((t[3]&8191u)<<16);memcpy(pending+0x200/4,t+8,256);
  if(mode==3)pending[0x200/4]^=1;if(mode==10)pending[0x74/4]^=1;if(mode==13)pending[0x78/4]^=1;
  if(mode==12){pending[0x74/4]|=0xff0000e0u;pending[0x78/4]|=0xe000e000u;}if(mode==9){memcpy(reg,pending,sizeof reg);dirty=0;}return 0;}
 case L_MUXR:assert(!a&&!b&&!d);top[0x1104/4]=6;return 0;case L_MUXW:assert(!a&&!b&&!d);top[0x110c/4]=2;return 0;
 case L_RDMA:case L_WDMA:{uint32_t*p=(uint32_t*)a;assert(!b&&!d);if(mode==5)return -1;unsigned off=fn==L_RDMA?0x1300:0x1700;
  assert(p[2]==SW_FRONT_STRIDE&&p[fn==L_RDMA?21:20]==480&&p[fn==L_RDMA?22:21]==LDC_TILE_H);
  assert(!p[7]&&p[8]);
  if(fn==L_RDMA){assert(p[11]==1&&!p[23]&&!p[24]&&p[25]==1);top[(off+16)/4]=mode==14?0x100:0x131;}
  else top[(off+16)/4]=mode==15?0x20:0x30;
  top[(off+32)/4]=((TEST_W-1)<<16)|(TEST_H-1);if(mode==21&&fn==L_WDMA)top[(off+32)/4]=(479u<<16)|1471u;
  top[(off+8)/4]=0x41;top[(off+36)/4]=(480u<<16)|LDC_TILE_H;if(mode==20&&fn==L_RDMA)top[(off+36)/4]^=1;
  top[off/4]=q.pa+p[0]-0x70000000u+(mode==34?4:0);top[off/4+1]=q.pa+p[1]-0x70000000u;return 0;}
 case L_START:{assert(armed&&locked&&configured&&commit_top&&commit_ldc&&(commit_barrier||mode==9)&&rdma_updated&&wdma_updated&&dma_rearms==2);
  const struct ep_standalone_start*s=(const void*)a;assert(s->module==0&&s->rdma[0]==0&&s->wdma[0]==0&&s->module_option==0);
  assert(s->rdma[1]==~0u&&s->rdma[2]==~0u&&s->wdma[1]==~0u&&s->wdma[2]==~0u&&s->wdma[3]==~0u);
  assert(manual_calls==3&&(top[0x1048/4]&0x300303u)==0x200202u);submitted=1;
  top[0x1008/4]=0x100001u;submit_pulses++;top[0x1008/4]=0x10000u;submit_pulses++;top[0x1008/4]=0;
  if(mode==22)top[0x1048/4]^=0x100000u; /* persistent mode mismatch */
  if(mode==37)top[0x172c/4]=1;
  return mode==41?-1:0;}
 case L_STOP:stops++;assert(submitted&&armed);if(mode==31)return -1;top[0x1114/4]=top[0x132c/4]=0;top[0x172c/4]=mode==23?1:0;top[0x1048/4]|=0x200202u;return 0;
 case L_VTOP:return (int)(q.pa+(uint32_t)a-0x70000000u+(mode==4?4:0));default:assert(0);return -1;}}
static void configure(struct lp_config*l,const char*source){FILE*f=fopen("/mnt/mmc/profile.bin","rb");assert(f&&fread(l,1,sizeof *l,f)==sizeof *l&&!fclose(f));lp_seal(l,source);assert(lp_valid(l,source));}

int main(void){
 struct sw_lease s={0};s.magic=SW_MAGIC;s.version=1;s.bytes=sizeof s;s.phase=SW_FRONT_READY;s.capacity=SW_BYTES;s.front_active=SW_FRONT_ACTIVE;
 s.input_offset=SW_INPUT;s.lut_offset=SW_LUT_OUT;s.jpeg_offset=SW_JPEG_OUT;s.table_offset=SW_TABLE;s.lease_offset=SW_LEASE;s.pa=0x98000000;s.va=0x87000000;s.nonce=777;s.owner_pid=getpid();s.owner_start_ticks=12345;s.index=(s.pa-0x94000000u)/16384;
 s.metadata[0]=35;s.metadata[1]=SW_BYTES/16384;s.front_return_barrier=s.frame_callbacks_retired=s.controller_slot_restored=1;
 memset(memory,0xa5,sizeof memory);full_hash(memory,SW_CONTROL,s.prefix_sha256);lut_make_table(full_table,1);full_hash(full_table,sizeof full_table,s.lut_sha256);memset(s.source_sha256,'c',64);sw_seal(&s);assert(sw_valid(&s)&&sw_owner_proof_publish(&s));full_workspace_original=s;
 struct sw_lease active=s;assert(sw_transition(&active,SW_FRONT_READY,SW_BACKEND_ACTIVE));
 q=(struct bitb_request){BITB_REQUEST_MAGIC,BITB_WIRE_VERSION,s.pa,SW_BYTES,999,456,s.va,0,0};q.crc=bitb_crc(&q,offsetof(struct bitb_request,crc));
 struct ldc_ops o={NULL,rd,wr,call,ctl,state,idle,barrier,0x1000,checkpoint,wait_override,coverage,ms,delay};
 for(mode=0;mode<=45;mode++){
  memset(top,0,sizeof top);memset(reg,0,sizeof reg);for(unsigned i=0;i<LDC_REGS;i++)reg[ldc_offsets[i]/4]=i*7+31;for(unsigned i=0;i<128;i++)reg[0x200/4+i]=i*13+1;
  top[0x1020/4]=top[0x1024/4]=8;top[0x1048/4]=0x840080u;if(mode==28)top[0x120/4]=1;if(mode==29)top[0x1214/4]=1;
  memcpy(before_top,top,sizeof top);memcpy(before_reg,reg,sizeof reg);
  memset(memory,0xa5,sizeof memory);memcpy(memory+SW_LEASE,&active,sizeof active);full_initialized=full_prepare_unsafe=locked=armed=submitted=resets=0;
  configured=dirty=commit_top=commit_ldc=commit_barrier=rdma_updated=wdma_updated=checkpoints=coverage_calls=stops=requests=now=ack_reads=wait_calls=manual_calls=submit_pulses=dma_rearms=0;
  struct lp_config l;configure(&l,s.source_sha256);if(mode==1){l.source_packet[31]^=1;lp_seal(&l,s.source_sha256);}
  struct bitb_result r={0};r.ready=r.paused_contract=1;r.alias_va=0x70000000;struct ldc_snapshot snapshot;struct ldc_config_audit audit;
  int rc=ldc_run(&q,&r,memory,&snapshot,&audit,&l,&o);assert(!locked);if(mode!=8)assert(!armed);
  unsigned success=mode==0||mode==9||mode==12||mode==32||mode==36||mode==37;
  assert((rc==0)==success);
  if(success){assert(r.safe_to_release&&resets==2&&r.restore_verified&&r.fresh_completion&&stops==1&&requests==1&&coverage_calls==1&&audit.stop_verified&&audit.dma_checkpoints==15&&checkpoints==5);assert(!memcmp(reg,before_reg,sizeof reg));assert(!memcmp(top,before_top,sizeof top));}
  if(!submitted)assert(r.safe_to_release&&stops==0&&requests==0);
  if(mode==6||mode==7||mode==16||mode==19||mode==22||mode==23||mode==24||mode==25||mode==26||mode==27||mode==30||mode==31||mode==38||mode==39||mode==41){assert(!r.safe_to_release&&resets==1&&!r.restore_verified&&r.quarantine_reason==1);}
  if(mode==6||mode==16||mode==19||mode==38){assert(r.error==BJ_WAIT&&!r.fresh_completion&&!stops&&!requests&&!coverage_calls);}
  if(mode==18||mode==40){assert(r.error==BJ_TILE_COVERAGE&&r.fresh_completion&&r.safe_to_release&&audit.stop_verified&&resets==2);}
  if(mode==8)assert(r.error==BJ_DISABLE&&armed&&!r.safe_to_release);
  if(mode==22||mode==41)assert(r.error==BJ_COMPARE&&submitted&&!wait_calls&&!stops);
  if(mode>=36&&mode<=40)assert(wait_calls==1);
  if(mode==23||mode==24||mode==25||mode==26||mode==27||mode==30||mode==31||mode==39)assert(r.error==BJ_DMA_DRAIN&&r.fresh_completion&&!coverage_calls);
  if(mode==24||mode==26||mode==27)assert(audit.stop_ms==50&&!audit.stop_verified&&requests==1&&!top[0x120/4]);
  if(mode==25)assert(audit.stop_ack_before==0x401&&!requests);
  if(mode==28||mode==29)assert(r.error==BJ_CONTEXT&&!submitted&&!resets);
  if(mode==35)assert(r.error==BJ_RESET&&!r.safe_to_release&&r.fresh_completion&&audit.stop_verified&&resets==2);
  if(mode==42||mode==43)assert(!submitted&&!wait_calls&&r.safe_to_release&&r.error==(mode==42?BJ_PREPARE:BJ_COMPARE));
  if(mode==44||mode==45)assert(!submitted&&!wait_calls&&r.safe_to_release&&r.error==BJ_RESET&&dma_rearms==(mode==44?1u:2u));
  if(submitted)assert(audit.dma_rearm_calls==2&&audit.dma_rearm_channel[0]==0&&audit.dma_rearm_channel[1]==16&&!audit.dma_rearm_status[0]&&!audit.dma_rearm_status[1]&&dma_rearms==2);
  if(!mode){FILE*f=fopen("/mnt/mmc/ldc-start-from-engine.bin","wx");assert(f&&fwrite(&snapshot.start,1,sizeof snapshot.start,f)==sizeof snapshot.start&&!fclose(f));
   f=fopen("/mnt/mmc/ldc-dma-from-engine.bin","wx");assert(f&&fwrite(snapshot.rdma,1,sizeof snapshot.rdma,f)==sizeof snapshot.rdma&&fwrite(snapshot.wdma,1,sizeof snapshot.wdma,f)==sizeof snapshot.wdma&&!fclose(f));
   /* Reuse the restored register RAM twice without resetting the model.
    * IRQ/ACK are fixtures; this checks invocation ordering, not chip FSMs. */
   for(unsigned round=1;round<=2;round++){
    assert(!locked&&!armed);full_initialized=full_prepare_unsafe=0;
    configured=dirty=commit_top=commit_ldc=commit_barrier=rdma_updated=wdma_updated=checkpoints=coverage_calls=stops=requests=now=ack_reads=wait_calls=manual_calls=submit_pulses=dma_rearms=resets=submitted=0;
    struct bitb_result rr={0};rr.ready=rr.paused_contract=1;rr.alias_va=0x70000000;struct ldc_snapshot ss;struct ldc_config_audit aa;
    assert(!ldc_run(&q,&rr,memory,&ss,&aa,&l,&o));
    assert(rr.safe_to_release&&rr.restore_verified&&rr.fresh_completion&&aa.stop_verified&&aa.dma_rearm_calls==2&&dma_rearms==2&&resets==2);
    assert(!memcmp(reg,before_reg,sizeof reg)&&!memcmp(top,before_top,sizeof top));
    printf("PASS retained-register LDC round=%u before-submit DMA_rearms=%u safe=%u\n",round,dma_rearms,rr.safe_to_release);
   }
  }
  if(mode==3||mode==10||mode==11||mode==13){assert(r.error==BJ_COMPARE&&audit.committed&&audit.mismatches&&!submitted);assert(audit.first_offset==(mode==10?0x74:mode==13?0x78:0x200));}
  printf("PASS native M2M state case=%u safe=%u submitted=%u resets=%u error=%u fresh=%u stop_ack=%u\n",mode,r.safe_to_release,submitted,resets,r.error,r.fresh_completion,audit.stop_verified);
 }return 0;
}
