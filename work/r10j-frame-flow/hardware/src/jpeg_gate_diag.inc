/* SPDX-License-Identifier: AGPL-3.0-only
 * Initial inactive status may settle to zero before the unchanged16x20ms
 * window. No literal status whitelist, and no ignored transition inside
 * that window. Native start/reset bits must stay inactive, clocks readable.
 * No reset, IRQ acknowledge, mask change or native job is added here.
 */
#ifndef JPEG_GATE_SETTLE_READ
#define JPEG_GATE_SETTLE_READ(c) ((c)->lut[0])
#endif
static FILE *gate_stream(struct native_context *c){return c->gate_log?c->gate_log:c->log;}
static void gate_result(struct native_context *c,const char *type,const char *reason,
 unsigned call,unsigned round,unsigned index,uint32_t address,uint32_t expected,uint32_t actual,int pass){
 FILE *f=gate_stream(c);if(!pass){c->gate_rejections++;c->gate_last_type=type;c->gate_last_reason=reason;}
 fprintf(f,"GATE_RESULT type=%s call=%u pass=%u reason=%s round=%u index=%u address=0x%08x expected=0x%08x actual=0x%08x errno=%d\n",type,call,pass,reason,round,index,address,expected,actual,errno);
 if(!pass&&c->log!=f)fprintf(c->log,"P21A_GATE_REJECT type=%s call=%u reason=%s round=%u index=%u address=0x%08x expected=0x%08x actual=0x%08x\n",type,call,reason,round,index,address,expected,actual);
}
static int queue_empty(const unsigned char *raw,uint32_t va){unsigned off=va-EP_KERNEL_STATE_VA;return !ep_sync_word(raw+off)&&ep_sync_word(raw+off+4)==va+4&&ep_sync_word(raw+off+8)==va+4;}
static int state(void *v,unsigned locked){
 struct native_context *c=v;unsigned call=++c->gate_state_calls;
 unsigned char raw[EP_KERNEL_STATE_BYTES],expected[EP_KERNEL_STATE_BYTES];uint32_t mutex[3];
#define STATE_FAIL(reason,i,addr,want,got) do{gate_result(c,"STATE",reason,call,locked,i,addr,want,got,0);return 0;}while(0)
 if(!owner_captured)STATE_FAIL("OWNER_SNAPSHOT_MISSING",0,0,1,0);
 if(!kernel_read(c,EP_KERNEL_STATE_VA,raw,sizeof raw))STATE_FAIL("KERNEL_READ_FAILED",0,EP_KERNEL_STATE_VA,sizeof raw,0);
 if(!kernel_read(c,EP_KERNEL_MUTEX_VA,mutex,12))STATE_FAIL("MUTEX_READ_FAILED",0,EP_KERNEL_MUTEX_VA,12,0);
 uint32_t want[3]={locked?0u:1u,EP_KERNEL_MUTEX_VA+4,EP_KERNEL_MUTEX_VA+4};
 for(unsigned i=0;i<3;i++)if(mutex[i]!=want[i])STATE_FAIL("MUTEX_CHANGED",i,EP_KERNEL_MUTEX_VA+4*i,want[i],mutex[i]);
 memcpy(expected,kernel_before,sizeof expected);uint32_t mask=locked==2?(c->lut_mode?0x80u:0x24u):0;memcpy(expected+0x74,&mask,4);
 uint32_t core=(c->lut_mode&&locked==3)?0x20u:0;memcpy(expected+0x64,&core,4);
 for(unsigned i=0;i<5;i++){uint32_t got=ep_sync_word(kernel_before+0x64+i*4);if(got)STATE_FAIL("BASELINE_MASK_NONZERO",i,EP_KERNEL_STATE_VA+0x64+i*4,0,got);}
 if(memcmp(raw,expected,sizeof raw)){
  unsigned off=0;while(off<sizeof raw&&raw[off]==expected[off])off++;off&=~3u;
  STATE_FAIL("KERNEL_WORD_CHANGED",off/4,EP_KERNEL_STATE_VA+off,ep_sync_word(expected+off),ep_sync_word(raw+off));
 }
 uint32_t queues[3]={c->lut_mode?0xc075551cu:0xc0755504u,0xc0755528u,0xc0755604u};
 for(unsigned q=0;q<(c->lut_mode?3u:1u);q++){
  uint32_t queue=queues[q];unsigned offset=queue-EP_KERNEL_STATE_VA;uint32_t qw[3]={0,queue+4,queue+4};
  for(unsigned i=0;i<3;i++){uint32_t got=ep_sync_word(raw+offset+4*i);if(got!=qw[i])STATE_FAIL("QUEUE_NOT_EMPTY",i,queue+4*i,qw[i],got);}
 }
 gate_result(c,"STATE","PASS",call,locked,0,0,0,0,1);return 1;
#undef STATE_FAIL
}
static int idle(void *v){
 struct native_context *c=v;unsigned call=++c->gate_idle_calls;uint32_t baseline[32],previous[5],js[12];
 FILE *f=gate_stream(c);uint32_t clock0=c->top[0x1020/4],clock1=c->top[0x1024/4];
 fprintf(f,"IDLE_ENTRY call=%u clock0=0x%08x clock1=0x%08x\n",call,clock0,clock1);
#define IDLE_FAIL(reason,round,i,addr,want,got) do{gate_result(c,"IDLE",reason,call,round,i,addr,want,got,0);return 0;}while(0)
 if(clock0&0x80)IDLE_FAIL("JPEG_CLOCK_GATED",0,0,0x1020,0,clock0&0x80);
 if(clock1&0x1e0000)IDLE_FAIL("JPEG_CLOCK_GATED",0,1,0x1024,0,clock1&0x1e0000);
 const unsigned jo[12]={0,4,8,0x280,0x800,0x804,0x808,0x80c,0x810,0x814,0x818,0x820};
 for(unsigned i=0;i<12;i++){js[i]=c->lut[jo[i]/4];fprintf(f,"JPEG_BASE call=%u index=%u offset=0x%04x value=0x%08x\n",call,i,jo[i],js[i]);}
 for(unsigned j=0;j<5;j++)previous[j]=c->rtos[(0xaac+j*4)/4];
 /* Read the already-used, non-acknowledging TOP status registers to make an
  * early rejection diagnosable. No gated JPEG register is read above/below. */
 for(unsigned j=0;j<5;j++)fprintf(f,"IRQ_ENTRY call=%u group=%u rtos=0x%08x top_mask=0x%08x raw=0x%08x mask_offset=0x%04x raw_offset=0x%04x\n",call,j,previous[j],c->top[(0x1070+j*4)/4],c->top[(0x10c8+j*4)/4],0x1070+j*4,0x10c8+j*4);
 for(unsigned j=0;j<5;j++)if(previous[j])IDLE_FAIL("RTOS_MASK_NONZERO",0,j,0x817d8aac+j*4,0,previous[j]);
 for(unsigned j=0;j<5;j++){
  uint32_t got=c->top[(0x1070+j*4)/4];if(got)IDLE_FAIL("TOP_MASK_DIFF",0,j,0x1070+j*4,0,got);
  got=c->top[(0x10c8+j*4)/4];if(got)IDLE_FAIL("RAW_PENDING",0,j,0x10c8+j*4,0,got);
 }
 uint32_t owner=c->rtos[0xc98/4];if(owner)IDLE_FAIL("RTOS_OWNER_NONZERO",0,0,0x817d8c98,0,owner);
 for(unsigned i=0;i<32;i++){
  unsigned channel=i/8,path=(i/4)%2,wr=(i/2)%2,plane=i%2;
  unsigned off=(wr?0x1700:0x1300)+channel*256+path*128+plane*4;
  baseline[i]=c->top[off/4];fprintf(f,"DMA_BASE call=%u index=%u offset=0x%04x value=0x%08x\n",call,i,off,baseline[i]);
 }
 /* Native _udd_ep_jpeg_ctrl_start/reset OR bit0/bit1. The other bits are
  * retained as evidence, not assigned an unproved busy meaning. A stable
  * inactive value was already accepted. Also permit an inactive initial
  * value to become ZERO before the full quiet window, without asserting
  * why it changed. Never accept a new nonzero status or an active control.
  * No reset, interrupt acknowledgement or register write occurs here. */
 if(js[0]&3u)IDLE_FAIL("JPEG_CONTROL_ACTIVE",0,0,0,0,js[0]&3u);
 if(js[4]&3u)IDLE_FAIL("JPEG_DMA_CONTROL_ACTIVE",0,4,0x800,0,js[4]&3u);
 uint32_t settled=JPEG_GATE_SETTLE_READ(c);
 if(settled!=js[0]){
  if(settled==0u){
   fprintf(f,"GATE_OBSERVATION type=IDLE call=%u reason=INITIAL_INACTIVE_STATUS_ZERO_BASELINE address=0x00000000 before=0x%08x after=0x00000000 read_only=1 following_stable_samples=16\n",call,js[0]);
   js[0]=settled;
  }else IDLE_FAIL("JPEG_INITIAL_STATUS_CHANGED",0,0,0,js[0],settled);
 }
 for(unsigned round=0;round<=16;round++){
  uint32_t gated=c->top[0x1020/4]&0x80u;if(gated)IDLE_FAIL("JPEG_CLOCK_GATED",round,0,0x1020,0,gated);
  gated=c->top[0x1024/4]&0x1e0000u;if(gated)IDLE_FAIL("JPEG_CLOCK_GATED",round,1,0x1024,0,gated);
  for(unsigned i=0;i<12;i++){uint32_t got=c->lut[jo[i]/4];if(js[i]!=got)IDLE_FAIL("JPEG_REGISTER_CHANGED",round,i,jo[i],js[i],got);}
  uint32_t otf=c->top[0x1118/4];if(otf)IDLE_FAIL("OTF_ACTIVE",round,0,0x1118,0,otf);
  otf=c->top[0x1218/4];if(otf)IDLE_FAIL("OTF_ACTIVE",round,1,0x1218,0,otf);
  for(unsigned j=0;j<5;j++){
   uint32_t got=c->rtos[(0xaac+j*4)/4];if(got!=previous[j])IDLE_FAIL("RTOS_MASK_CHANGED",round,j,0x817d8aac+j*4,previous[j],got);
   got=c->top[(0x1070+j*4)/4];if(got!=previous[j])IDLE_FAIL("TOP_MASK_DIFF",round,j,0x1070+j*4,previous[j],got);
   got=c->top[(0x10c8+j*4)/4];if(got)IDLE_FAIL("RAW_PENDING",round,j,0x10c8+j*4,0,got);
  }
  for(unsigned i=0;i<32;i++){
   unsigned channel=i/8,path=(i/4)%2,wr=(i/2)%2,plane=i%2;
   unsigned off=(wr?0x1700:0x1300)+channel*256+path*128+plane*4;uint32_t got=c->top[off/4];
   if(got!=baseline[i])IDLE_FAIL("DMA_ADDRESS_CHANGED",round,i,off,baseline[i],got);
  }
  if(round<16){struct timespec t={0,20000000};if(nanosleep(&t,0))IDLE_FAIL("SAMPLE_SLEEP_FAILED",round,0,0,0,(uint32_t)errno);}
 }
 gate_result(c,"IDLE","PASS",call,16,0,0,0,0,1);
 fprintf(c->log,"IDLE_GATE: OTF off; 16 stable 20ms intervals including final boundary; no raw pending; no conflicting owner\n");return 1;
#undef IDLE_FAIL
}
