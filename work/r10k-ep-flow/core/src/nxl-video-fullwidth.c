/* SPDX-License-Identifier: AGPL-3.0-only. NX5001.12 full-width UHD experiment.
 * A 2MiB transient block runs guarded transactions on the RTOS core.
 * Fixed OEM TMC code/data are leased until reboot, never kept in that CAP.
 * Existing OEM sensor mode, PP, IPC/resize, 3 work frames / 10 codec frames.
 */
#pragma clang diagnostic ignored "-Wunused-variable"
#include "native_allocator.inc"
#include <sys/stat.h>
#include <sys/file.h>
#include <strings.h>
#include "front_layout.h"
#include "bridge_guards.h"
#include "bridge_core.h"
#include "../../common/video_fullwidth_contract.h"
#include "../../common/video_mode_patch.h"
#include "rtos_video_fullwidth_blob.h"
#include "video_fullwidth_guards.h"
#include "../../common/allocator_fresh_read.h"
#include "../../common/video_allocator_proof.h"
static uint32_t mcb_base,packet_count,send_calls,nonce;
static unsigned char expected[65536],console_raw[P33_CAPTURE_BYTES],console_text[P33_CAPTURE_BYTES+1];
static volatile unsigned char*cap_alias,*console_map;static volatile uint32_t*slot_map;
static int sma_fd=-1,rwfd=-1;static FILE*command_log;static unsigned console_bytes;
static int slot_published,rtos_entered,rtos_retired,cap_released,ui_thawed=1,quarantined;
static struct vfw_reply receipt;static struct vfw_snapshot observed;static struct vfw_ipc_route observed_route;
/* Complete NC code/context stores before publishing a pointer or GO to the
 * other core; the Linux-only ordering primitive is not the publication fence. */
static inline void video_publish_barrier(void){__asm__ volatile("dsb sy\n\tdmb sy":::"memory");}
struct held_thread {int tid,attached,stopped,signal;};
static struct held_thread held[192];static unsigned held_count;
static int physical_read(void*c,uint32_t at,void*out,unsigned bytes){
 (void)c;if(!bytes||bytes>8192u||at<0x80000000u||(uint64_t)at+bytes>0x94000000ULL)return 0;
 uint32_t page=at&~4095u;unsigned extent=((at&4095u)+bytes+4095u)&~4095u;
 void*p=mmap(NULL,extent,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)page);if(p==MAP_FAILED)return 0;
 const volatile unsigned char*s=(const volatile unsigned char*)p+(at&4095u);for(unsigned i=0;i<bytes;i++)((unsigned char*)out)[i]=s[i];video_publish_barrier();return !munmap(p,extent);
}
static int section_kind(uint32_t at){
 uint32_t d;if(!physical_read(NULL,0x81000000u+4*(at>>20),&d,4)||(d&3u)!=2u||(d&0x0004e010u)||!(d&0x400u)||(d&0xfff00000u)!=(at&0xfff00000u))return 0;
 unsigned n=((d>>10)&4u)|((d>>2)&3u);uint32_t p=0x000a8aa4u,m=0x44e048e0u;if(((p>>(2*n))&3u)!=2u)return 0;unsigned a=(m>>(2*n))&3u,b=(m>>(16+2*n))&3u;return !a&&!b?1:a==1&&b==1?2:0;
}
static int code_guards(void){
 int replacement=vfw_block_state(physical_read,NULL);if(replacement<0)return 0;
 unsigned char b[2048];for(unsigned i=0;i<sizeof vfw_code_guards/sizeof*vfw_code_guards;i++){
  const struct vfw_code_guard*g=vfw_code_guards+i;if(g->bytes>sizeof b||!physical_read(NULL,g->address,b,g->bytes))return 0;
  if(g->address<=VFW_MAP_WORD&&(uint64_t)g->address+g->bytes>VFW_MAP_WORD){uint32_t w;memcpy(&w,b+VFW_MAP_WORD-g->address,4);if(w!=VFW_MAP_CROP&&w!=VFW_MAP_FULL)return 0;w=VFW_MAP_CROP;memcpy(b+VFW_MAP_WORD-g->address,&w,4);}
  if(g->address<=VFW_BACKEND_WORD&&(uint64_t)g->address+g->bytes>VFW_BACKEND_WORD){uint32_t w;memcpy(&w,b+VFW_BACKEND_WORD-g->address,4);if(w!=VFW_BACKEND_UD&&w!=VFW_BACKEND_SCALE)return 0;w=VFW_BACKEND_UD;memcpy(b+VFW_BACKEND_WORD-g->address,&w,4);}
  if(g->address<=VFW_LDC_POLICY_WORD&&(uint64_t)g->address+g->bytes>VFW_LDC_POLICY_WORD){uint32_t w;memcpy(&w,b+VFW_LDC_POLICY_WORD-g->address,4);if(w!=VFW_LDC_POLICY_UD&&w!=VFW_LDC_POLICY_SCALE)return 0;w=VFW_LDC_POLICY_UD;memcpy(b+VFW_LDC_POLICY_WORD-g->address,&w,4);}
  const uint32_t at[2]={VFW_LCAC_MASK_WORD,VFW_LCAC_OR_WORD},stock[2]={VFW_LCAC_MASK_STOCK,VFW_LCAC_OR_STOCK},full[2]={VFW_LCAC_MASK_FULL,VFW_LCAC_OR_FULL};for(unsigned k=0;k<2;k++)if(g->address<=at[k]&&(uint64_t)g->address+g->bytes>at[k]){uint32_t w;memcpy(&w,b+at[k]-g->address,4);if(w!=stock[k]&&w!=full[k])return 0;memcpy(b+at[k]-g->address,stock+k,4);}
  for(unsigned j=0;j<VFW_BLOCK_COUNT;j++){const struct vfw_block*x=vfw_blocks+j;uint32_t lo=g->address>x->address?g->address:x->address,hi=g->address+g->bytes<x->address+x->bytes?g->address+g->bytes:x->address+x->bytes;if(lo<hi)memcpy(b+lo-g->address,(const unsigned char*)vfw_stock(x)+lo-x->address,hi-lo);}
  if(vfw_hash(b,g->bytes)!=g->hash)return 0;
 }return 1;
}
static int video_entry(unsigned index,uint32_t e[4]){return index<ALLOC_BLOCKS&&af_read_words(devfd,AF_TABLE_PA+index*16u,e,4);}
static int video_owned(void){uint32_t e[4];return ownership_verified&&video_entry(index_owned,e)&&!memcmp(e,owned_meta,16)&&response[2]==VFW_CAP_BYTES&&response[3]==2&&(e[0]&255u)==0x23u&&e[1]==VFW_CAP_BLOCKS&&region_ok(response[0],VFW_CAP_BYTES,1,0);}
static int safe_call(void){return !api_inflight&&!native_signal&&trace_stopped&&call_boundary_active&&!stop_requested&&video_owned();}
static int invoke4(uint32_t fn,uint32_t a,uint32_t b,uint32_t c,uint32_t d,unsigned timeout){
 struct alloc_regs r=alloc_call3(&call_original,fn,a,b,c);r.r[3]=d;trace_error=NULL;if(!set_regs(&r))return 0;native_calls++;api_inflight=1;
 if(!resume()||!wait_stop(timeout,SIGTRAP)||!verify_breakpoint(1)||!at_return(&r))return 0;api_inflight=0;return 1;
}
static int p39_packet_extent(unsigned slot,unsigned length,unsigned*out){if(slot>=12||!length||length>=128)return 0;*out=P39_PACKET_START+slot*128u;return *out+length+1<VFW_CAP_BYTES;}
#include "capture_console.inc"
static int bridge_context(void){
 for(unsigned i=0;i<region_count;i++)if(!regions[i].offset&&!strcmp(regions[i].perms,"r-xp")&&!strcmp(regions[i].path,"/usr/lib/libmulticore-bridge.so")){if(mcb_base&&mcb_base!=regions[i].lo)return 0;mcb_base=regions[i].lo;}
 if(!mcb_base||!fingerprint("/usr/lib/libmulticore-bridge.so",P33_MCB_BYTES,P33_MCB_FNV)||!guards(mcb_base,p33_mcb_guards,sizeof p33_mcb_guards/sizeof*p33_mcb_guards))return 0;
 uint32_t holder=mcb_base+0x12730u+68u,bucket=holder+29u*20u,a[5],b[5],guard,type;unsigned char alive;
 if(!read_word(mcb_base+0x1272c,&guard)||!(guard&1)||!read_word(holder,&type)||type!=mcb_base+0x12050u||!read_process(bucket+4,a,20)||!read_process(bucket+4,b,20)||memcmp(a,b,20)||!read_process(mcb_base+0x12ad8,&alive,1)||alive!=1)return 0;
 for(unsigned i=0;i<5;i++)if(a[i])return 0;return 1;
}
static int busy(void){
 char path[100];snprintf(path,sizeof path,"/proc/%d/fd",pid);DIR*d=opendir(path);if(!d)return -1;struct dirent*e;int result=0;
 while((e=readdir(d))){char*end;long f=strtol(e->d_name,&end,10);if(*end||f<0)continue;char target[512],info[512];snprintf(path,sizeof path,"/proc/%d/fd/%ld",pid,f);ssize_t n=readlink(path,target,sizeof target-1);if(n<0){if(errno==ENOENT)continue;result=-1;break;}if(n>=(ssize_t)sizeof target-1){result=-1;break;}target[n]=0;
  const char*ext=strrchr(target,'.');if(!ext||(strcasecmp(ext,".mp4")&&strcasecmp(ext,".srw")&&strcasecmp(ext,".jpg")&&strcasecmp(ext,".jpeg")))continue;
  snprintf(path,sizeof path,"/proc/%d/fdinfo/%ld",pid,f);FILE*in=fopen(path,"r");if(!in){result=-1;break;}size_t got=fread(info,1,sizeof info-1,in);int ok=!ferror(in)&&got<sizeof info-1;if(fclose(in))ok=0;info[got]=0;char*q=strstr(info,"flags:");unsigned flags;
  if(!ok||!q||sscanf(q+6,"%o",&flags)!=1){result=-1;break;}if((flags&O_ACCMODE)!=O_RDONLY){result=1;break;}
 }closedir(d);return result;
}
static int constructor(void){uint32_t context[24];if(!read_process(0xe4400u,context,sizeof context))return 0;int mode=vm_pair(context+10);context[10]=0xe3a08e87u;context[11]=0xe3a0ac0fu;return mode&&vfw_hash(context,sizeof context)==VFW_APP_CONTEXT_HASH?mode:0;}
static int slot_original(void){uint32_t words[2];return physical_read(NULL,VFW_SLOT,words,8)&&words[0]==VFW_ORIGINAL&&!words[1];}
static const char*idle_reason="IDLE_NOT_CHECKED";
static unsigned idle_checks,idle_snapshot_valid,idle_active;
static uint32_t idle_task,idle_selected,idle_pending[4],idle_driver,idle_work,idle_src,idle_power_current,idle_power_video;
static unsigned char idle_state;static int idle_media=-1;
static int idle_fail(const char*why){idle_reason=why;return 0;}
static int quiet_source(void){
 idle_checks++;idle_snapshot_valid=0;idle_media=-1;
 if(!physical_read(NULL,P39_TASK,&idle_task,4)||!physical_read(NULL,P39_TASK+3364,&idle_state,1)||!physical_read(NULL,P39_TASK+3204,idle_pending,8)||!physical_read(NULL,P39_TASK+3284,idle_pending+2,8)||!physical_read(NULL,0x8142d154u+3444,&idle_selected,4))return idle_fail("CAMERA_IDLE_STATE_UNREADABLE");
 if(idle_task!=0x80592640u)return idle_fail("CAMERA_TASK_IDENTITY_CHANGED");
 if(idle_state!=0&&idle_state!=2&&idle_state!=7)return idle_fail("CAMERA_PHOTO_TASK_BUSY");
 for(unsigned i=0;i<4;i++)if(idle_pending[i])return idle_fail("CAMERA_PHOTO_TASK_PENDING_BUSY");
 if(idle_selected==0x8142df3cu)return idle_fail("CAMERA_CAPTURE_PIPELINE_BUSY");
 idle_media=busy();if(idle_media)return idle_fail(idle_media>0?"CAMERA_MEDIA_FILE_WRITE_BUSY":"CAMERA_MEDIA_STATUS_UNREADABLE");
 if(!vfw_capture_route(physical_read,NULL,&observed,&observed_route))return idle_fail("CAMERA_IDLE_PROFILE_UNSTABLE");
 idle_snapshot_valid=1;idle_driver=observed.driver_modes;idle_active=vfw_movie_active(&observed);idle_work=observed.work[0];idle_src=observed.src[0];idle_power_current=observed.power_current;idle_power_video=observed.power_video;
 if(!vfw_route_mode(&observed,&observed_route)||!vfw_budget(&observed))return idle_fail("CAMERA_IDLE_PROFILE_OR_BUDGET_UNKNOWN");
 if(idle_active)return idle_fail("PHOTO_MODE_REQUIRED_MOVIE_PIPELINE_ACTIVE");
 idle_reason="CAMERA_IDLE_VERIFIED";return 1;
}
static void idle_audit(const char*dir){
 char p[1200];snprintf(p,sizeof p,"%s/idle-check.json",dir);FILE*f=fopen(p,"wx");if(!f)return;
 fprintf(f,"{\"format\":\"NXL_VIDEO_IDLE_CHECK1\",\"checks\":%u,\"reason\":\"%s\",\"task_type\":%u,\"task_state\":%u,\"pending\":[%u,%u,%u,%u],\"selected\":%u,\"writable_media\":%d,\"snapshot_valid\":%u,\"driver_modes\":%u,\"movie_active_mask\":%u,\"movie_work_address\":%u,\"movie_source_address\":%u,\"power_current\":%u,\"power_video\":%u,\"power_pointer_is_recording_flag\":false}\n",idle_checks,idle_reason,idle_task,idle_state,idle_pending[0],idle_pending[1],idle_pending[2],idle_pending[3],idle_selected,idle_media,idle_snapshot_valid,idle_driver,idle_active,idle_work,idle_src,idle_power_current,idle_power_video);fclose(f);
}
static const char*video_allocator_reason="VIDEO_ALLOCATOR_NOT_CHECKED";
static struct vfa_head video_allocator_head;
static unsigned video_table_free,video_bank_largest,video_allocator_attempts;
static int video_head(struct vfa_head*out){
 uint32_t root[10],head[66],tail[3],upper;
 if(!out||!af_read_words(devfd,0x817dc498u,root,10)||!af_read_words(devfd,VFW_ALLOCATOR,head,66)||!af_read_words(devfd,0x81eac908u,tail,3)||!af_read_words(devfd,0x81eacda0u,&upper,1))return 0;
 if(root[0]!=0x80574230u||root[1]!=0x805742acu||root[9]!=VFW_ALLOCATOR||head[0]!=0x80574b60u||head[65]!=0x80574c48u||tail[0]!=0x94000000u||tail[1]!=ALLOC_BLOCKS||head[CAP_MANAGER_BANK2_OFFSET/4]<0x94000000u||head[CAP_MANAGER_BANK2_OFFSET/4]>=upper||upper>0xbb6cc000u||((head[CAP_MANAGER_BANK2_OFFSET/4]|upper)&16383u))return 0;
 out->bank2=head[CAP_MANAGER_BANK2_OFFSET/4];out->upper=upper;out->reported_free=tail[2];return 1;
}
static void video_allocator_audit(const char*dir){
 char p[1200];snprintf(p,sizeof p,"%s/allocator-check.json",dir);FILE*f=fopen(p,"wx");if(!f)return;
 fprintf(f,"{\"format\":\"NXL_VIDEO_ALLOCATOR2\",\"reason\":\"%s\",\"attempts\":%u,\"total_blocks\":%u,\"OEM_reported_free\":%u,\"partition_free_blocks\":%u,\"bank2\":%u,\"upper\":%u,\"bank2_largest_blocks\":%u,\"required_blocks\":%u,\"aggregate_counter_is_capacity_proof\":false,\"aggregate_counter_writes\":0}\n",video_allocator_reason,video_allocator_attempts,ALLOC_BLOCKS,video_allocator_head.reported_free,video_table_free,video_allocator_head.bank2,video_allocator_head.upper,video_bank_largest,VFW_CAP_BLOCKS);fclose(f);
}
static int ownership_baseline(const char*dir){
 baseline=malloc(ALLOC_BLOCKS*16u);baseline_again=malloc(ALLOC_BLOCKS*16u);if(!baseline||!baseline_again){video_allocator_reason="VIDEO_ALLOCATOR_HOST_MALLOC_FAILED";return 0;}
 for(unsigned attempt=0;attempt<8&&!stop_requested;attempt++){
  struct vfa_head a,b,c;video_allocator_attempts++;
  if(!video_head(&a)||!af_read_words(devfd,AF_TABLE_PA,baseline,AR_TABLE_WORDS)||!video_head(&b)||!af_read_words(devfd,AF_TABLE_PA,baseline_again,AR_TABLE_WORDS)||!video_head(&c)){video_allocator_reason="VIDEO_ALLOCATOR_IDENTITY_OR_READ_FAILED";return 0;}
  video_allocator_head=c;
  if(!memcmp(&a,&b,sizeof a)&&!memcmp(&b,&c,sizeof b)&&!memcmp(baseline,baseline_again,ALLOC_BLOCKS*16u)){
   char p[1200];snprintf(p,sizeof p,"%s/allocator-before.bin",dir);FILE*f=fopen(p,"wx");if(!f){video_allocator_reason="VIDEO_ALLOCATOR_AUDIT_OPEN_FAILED";return 0;}int saved=fwrite(baseline,1,ALLOC_BLOCKS*16u,f)==ALLOC_BLOCKS*16u;if(fclose(f))saved=0;if(!saved){video_allocator_reason="VIDEO_ALLOCATOR_AUDIT_WRITE_FAILED";return 0;}
   if(!vfa_partition(baseline,ALLOC_BLOCKS,&video_table_free)||!cap_bank2_largest(baseline,ALLOC_BLOCKS,c.bank2,c.upper,&video_bank_largest)){video_allocator_reason="VIDEO_ALLOCATOR_PARTITION_INVALID";return 0;}
   if(video_bank_largest<VFW_CAP_BLOCKS){video_allocator_reason="VIDEO_ALLOCATOR_BANK2_FRAGMENTED";return 0;}
   free_before=video_table_free;video_allocator_reason="VIDEO_ALLOCATOR_PARTITION_AND_BANK2_VERIFIED";return 1;
  }
  video_allocator_reason="VIDEO_ALLOCATOR_SNAPSHOT_UNSTABLE";pause_ms(30);
 }return 0;
}
static int confirm_owned(void){
 uint32_t pa=0;int found=0;for(unsigned i=0;i<mapping_count;i++){uint32_t p;if(mem_translate(mappings+i,response[0],VFW_CAP_BYTES,&p)){if(found&&p!=pa)return 0;pa=p;found=1;}}
 struct vfa_head fresh;if(!video_head(&fresh))return 0;uint32_t upper=fresh.upper;if(!found||response[1]!=pa||!pa||(pa&16383u)||response[2]!=VFW_CAP_BYTES||response[3]!=2||pa<0x94000000u||(uint64_t)pa+VFW_CAP_BYTES>upper||pa+VFW_CAP_BYTES>0xbb6cc000u)return 0;
 index_owned=(pa-0x94000000u)/16384u;if(index_owned>ALLOC_BLOCKS-VFW_CAP_BLOCKS||!video_entry(index_owned,owned_meta)||(owned_meta[0]&255u)!=0x23u||owned_meta[1]!=VFW_CAP_BLOCKS)return 0;
 unsigned covered=0;for(unsigned i=0;i<index_owned+VFW_CAP_BLOCKS;i++){unsigned kind=baseline[4*i]&255u,span=baseline[4*i+1];if(kind){if(!span||span>ALLOC_BLOCKS-i)return 0;if(i+span>covered)covered=i+span;}if(i>=index_owned&&(i<covered||kind||span))return 0;}
 ownership_verified=1;return video_owned();
}
static int setup_alias(void){
 fprintf(log_file,"FULLWIDTH_MMU cap=0x%08x kind=%d slot_kind=%d mapping_literal_kind=%d profile19=0x%08x kind=%d\n",response[1],section_kind(response[1]),section_kind(VFW_SLOT),section_kind(VFW_MAP_WORD),observed.profile19,section_kind(observed.profile19));fflush(log_file);
 for(uint32_t at=response[1]&~0xfffffu;at<(uint64_t)response[1]+VFW_CAP_BYTES;at+=0x100000u)if(section_kind(at)!=1)return 0;
 if(!section_kind(VFW_SLOT)||!section_kind(VFW_MAP_WORD)||!section_kind(observed.profile19))return 0;
 sma_fd=open("/dev/d5_sma",O_RDWR|O_SYNC|O_CLOEXEC);if(sma_fd<0)return 0;void*p=mmap(NULL,VFW_CAP_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,sma_fd,(off_t)(uint64_t)response[1]);if(p==MAP_FAILED)return 0;cap_alias=p;
 memset(expected,0,sizeof expected);memcpy(expected,vfw_code,sizeof vfw_code);struct vfw_context command={VFW_MAGIC,VFW_SCHEMA,nonce,response[1],VFW_CAP_BYTES,0,observed.root,observed.profile6,observed.profile19,observed.power_manager,observed.power_video,observed.power_ops,observed.power_entries,observed.power_scene_word,{0}};memcpy(expected+VFW_CONTEXT,&command,sizeof command);
 for(unsigned i=0;i<sizeof expected;i++)cap_alias[i]=expected[i];video_publish_barrier();for(unsigned i=0;i<sizeof expected;i++)if(cap_alias[i]!=expected[i])return 0;
 p=mmap(NULL,P33_CONSOLE_BYTES,PROT_READ,MAP_SHARED,devfd,(off_t)(uint64_t)P33_CONSOLE_BASE);if(p==MAP_FAILED)return 0;console_map=p;
 return volatile_word(console_map)<P33_CONSOLE_CAPACITY&&volatile_word(console_map+4)<P33_CONSOLE_CAPACITY;
}
static int cache_packet(uint32_t cookie,uint32_t at,uint32_t bytes){
 unsigned off;if(!safe_call()||!p39_packet_extent(packet_count,12,&off))return 0;uint32_t data[3]={cookie,at,bytes};memcpy(expected+off,data,12);for(unsigned i=0;i<12;i++)cap_alias[off+i]=expected[off+i];video_publish_barrier();
 unsigned char got[12];struct alloc_regs regs;if(!invoke3(libc_base+ALLOC_MEMCPY,receive_buffer+48,response[0]+off,12,3000)||!get_regs(&regs)||regs.r[0]!=receive_buffer+48||!read_process(receive_buffer+48,got,12)||memcmp(got,data,12))return 0;
 packet_count++;if(!invoke4(mcb_base+P33_MCB_SEND,129,399,12,response[0]+off,3000)||!get_regs(&regs)||regs.r[0])return 0;send_calls++;fprintf(command_log,"CACHE_OR_COMMAND cookie=%08x address=%08x bytes=%u send_is_not_completion=1\n",cookie,at,bytes);fflush(command_log);return 1;
}
static int publish_slot(void){
 if(!safe_call()||!slot_original())return 0;rwfd=open("/dev/mem",O_RDWR|O_SYNC|O_CLOEXEC);if(rwfd<0)return 0;void*p=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_SHARED,rwfd,(off_t)(uint64_t)(VFW_SLOT&~4095u));if(p==MAP_FAILED)return 0;slot_map=(volatile uint32_t*)((volatile unsigned char*)p+(VFW_SLOT&4095u));if(slot_map[0]!=VFW_ORIGINAL||slot_map[1])return 0;slot_published=1;video_publish_barrier();slot_map[0]=response[1];video_publish_barrier();return slot_map[0]==response[1]&&!slot_map[1];
}
static int read_reply(unsigned phase,unsigned timeout){
 uint64_t until=ns()+(uint64_t)timeout*1000000u;while(!stop_requested&&ns()<until){struct vfw_reply a,b;for(unsigned i=0;i<sizeof a;i++)((unsigned char*)&a)[i]=cap_alias[VFW_REPLY+i];video_publish_barrier();for(unsigned i=0;i<sizeof b;i++)((unsigned char*)&b)[i]=cap_alias[VFW_REPLY+i];if(!memcmp(&a,&b,sizeof a)&&vfw_reply_valid(&a,nonce)){receipt=a;if(a.phase==phase)return 1;if(a.phase==2)return 0;}pause_ms(1);}return 0;
}
static int thread_untraced(int t){char p[100],line[512];snprintf(p,sizeof p,"/proc/%d/task/%d/status",pid,t);FILE*f=fopen(p,"r");if(!f)return 0;int tracer=-1;char state=0;while(fgets(line,sizeof line,f)){if(!strncmp(line,"TracerPid:",10))sscanf(line+10,"%d",&tracer);if(!strncmp(line,"State:",6))sscanf(line+6," %c",&state);}int ok=!ferror(f);if(fclose(f))ok=0;return ok&&tracer==0&&state&&state!='T'&&state!='t'&&state!='Z'&&state!='X';}
static int thread_wait(struct held_thread*t,uint64_t end){while(!stop_requested&&ns()<end){int status;pid_t got=waitpid(t->tid,&status,__WALL|WNOHANG);if(got==t->tid){if(!WIFSTOPPED(status))return 0;t->stopped=1;int ours=WSTOPSIG(status)==SIGTRAP&&((unsigned)status>>16)==PTRACE_EVENT_STOP;t->signal=ours?0:WSTOPSIG(status);return ours;}if(got<0&&errno!=EINTR)return 0;pause_ms(1);}return 0;}
static int freeze_ui(void){
 uint64_t end=ns()+700000000ULL;ui_thawed=0;
 for(unsigned round=0;round<4;round++){unsigned before=held_count;char p[100];snprintf(p,sizeof p,"/proc/%d/task",pid);DIR*d=opendir(p);if(!d)return 0;struct dirent*e;int ok=1;
  while((e=readdir(d))){char*x;long t=strtol(e->d_name,&x,10);if(*x||t<=1||t==tid)continue;unsigned i;for(i=0;i<held_count;i++)if(held[i].tid==t)break;if(i<held_count)continue;if(held_count>=192||!thread_untraced((int)t)){ok=0;break;}held[held_count++].tid=(int)t;}closedir(d);if(!ok)return 0;
  for(unsigned i=0;i<held_count;i++){struct held_thread*t=held+i;if(t->attached)continue;if(ptrace(PTRACE_SEIZE,t->tid,0,0))return 0;t->attached=1;if(ptrace(PTRACE_INTERRUPT,t->tid,0,0)||!thread_wait(t,end))return 0;}
  if(round&&before==held_count)return held_count>5;if(ns()>=end)return 0;
 }return 0;
}
static int thaw_ui(void){
 int ok=1;sig_atomic_t saved=stop_requested;stop_requested=0;uint64_t end=ns()+700000000ULL;
 for(unsigned i=held_count;i>0;i--){struct held_thread*t=held+i-1;if(!t->attached)continue;if(!t->stopped){(void)ptrace(PTRACE_INTERRUPT,t->tid,0,0);(void)thread_wait(t,end);}if(ptrace(PTRACE_DETACH,t->tid,0,t->signal)){char p[100];struct stat st;snprintf(p,sizeof p,"/proc/%d/task/%d",pid,t->tid);if(errno==ESRCH&&lstat(p,&st)<0&&errno==ENOENT)t->attached=0;else ok=0;}else t->attached=0;}
 stop_requested=saved;ui_thawed=ok;return ok;
}
static int return_cap(const char*dir){
 if(!ownership_verified||!video_owned()||!rtos_retired||!slot_original()||!ui_thawed)return 0;
 if(cap_alias){if(munmap((void*)cap_alias,VFW_CAP_BYTES))return 0;cap_alias=NULL;}
 trace_phase="video_transient_cap_release";struct alloc_regs regs;if(!invoke(cap_base+ALLOC_RELEASE,response[0],5000)||!get_regs(&regs)||regs.r[0])return 0;release_done=1;
 /* A normal stack payload, not a released CAP alias. Publish allocator
  * metadata once via the same OEM cache attribute used by the SRW worker. */
 uint32_t adapter=0,vptr=0,payload[3]={0,VFW_ALLOCATOR,0xb0600u};if(!read_word(cap_base+0xb33dcu,&adapter)||!read_word(adapter,&vptr)||vptr!=cap_base+0xb0240u||pwrite(procfd,payload,12,(off_t)(receive_buffer+48))!=12)return 0;
 if(!invoke4(cap_base+0x83ea4u,adapter,399,12,receive_buffer+48,3000)||!get_regs(&regs)||regs.r[0])return 0;
 uint64_t until=ns()+3000000000ULL;unsigned consecutive=0;while(!stop_requested&&ns()<until){
  struct vfa_head a,b,c;unsigned actual_free=0;
  int same=video_head(&a)&&af_read_words(devfd,AF_TABLE_PA,baseline,AR_TABLE_WORDS)&&video_head(&b)&&af_read_words(devfd,AF_TABLE_PA,baseline_again,AR_TABLE_WORDS)&&video_head(&c)&&!memcmp(&a,&b,sizeof a)&&!memcmp(&b,&c,sizeof b)&&!memcmp(baseline,baseline_again,ALLOC_BLOCKS*16u)&&vfa_partition(baseline,ALLOC_BLOCKS,&actual_free)&&vfw_returned(baseline,ALLOC_BLOCKS,actual_free,index_owned);
  consecutive=same?consecutive+1:0;if(consecutive>=3){cap_released=return_verified=1;event("VIDEO_TRANSIENT_CAP_RETURN_VERIFIED");char p[1200];snprintf(p,sizeof p,"%s/CAP.returned",dir);FILE*f=fopen(p,"wx");if(f){fprintf(f,"bytes=%u\nblocks=%u\nfresh_full_table_pairs=%u\npartition_free_blocks=%u\nOEM_reported_free=%u\naggregate_counter_writes=0\n",VFW_CAP_BYTES,VFW_CAP_BLOCKS,consecutive,actual_free,c.reported_free);fclose(f);}return 1;}pause_ms(same?30:100);
 }return 0;
}
static int lockfile(const char*p){int f=open(p,O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);if(f<0)return -1;struct stat s;if(fstat(f,&s)||!S_ISREG(s.st_mode)||flock(f,LOCK_EX|LOCK_NB)){close(f);return -1;}return f;}
static int snapshot_file(const char*dir,const char*name){char p[1200];snprintf(p,sizeof p,"%s/%s",dir,name);FILE*f=fopen(p,"wx");if(!f)return 0;int ok=fwrite(&observed,1,sizeof observed,f)==sizeof observed&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;}
static int tmc_snapshot_file(const char*dir){uint32_t words[VFW_TMC_BYTES/4];if(!physical_read(NULL,VFW_TMC_DATA,words,sizeof words))return 0;char p[1200];snprintf(p,sizeof p,"%s/before-tmc.bin",dir);FILE*f=fopen(p,"wx");if(!f)return 0;int ok=fwrite(words,1,sizeof words,f)==sizeof words&&!fflush(f);if(fclose(f))ok=0;return ok;}
static void result(int ok,int state,const char*dir){
 if(dir&&*dir){idle_audit(dir);video_allocator_audit(dir);}
 int vm=procfd>=0?constructor():0;const char*name=state==VFW_FULL&&vm==VM_UHD?"FULL":(state==VFW_LEGACY_FULL||state==VFW_LEGACY_SCALE||state==VFW_LEGACY_UPDATE||state==VFW_LEGACY_LDC||state==VFW_LEGACY_LCAC||state==VFW_LEGACY_WIDTH||state==VFW_LEGACY_BOUNDARY)?"LEGACY":state==VFW_STOCK&&vm==VM_UHD?"UHD":state==VFW_STOCK&&vm==VM_25K?"2K":"UNKNOWN";unsigned width=!strcmp(name,"2K")?2560u:!strcmp(name,"UNKNOWN")?0u:3840u,height=!strcmp(name,"2K")?1440u:!strcmp(name,"UNKNOWN")?0u:2160u;
 printf("NXL_VIDEO_MODE1\nstate=%s\nwidth=%u\nheight=%u\npid=%d\nrecording_or_capture=%d\nreason=%s\nwrites=%u\nrollback_verified=%u\nthreads_detached=%u\nend=NXL_VIDEO_MODE_END1\n",name,width,height,pid,pid?busy():-1,reason,receipt.writes,receipt.rollback_verified,ui_thawed&&(!tid||detached_ok));
 if(dir&&*dir){char p[1200];snprintf(p,sizeof p,"%s/result.json",dir);FILE*f=fopen(p,"wx");if(f){fprintf(f,"{\"format\":\"NXL_VIDEO_FULLWIDTH1\",\"ok\":%s,\"reason\":\"%s\",\"state\":\"%s\",\"RTOS_entered\":%u,\"RTOS_retired_by_original_console\":%u,\"transient_CAP_bytes\":%u,\"CAP_return_verified\":%u,\"UI_thawed\":%u,\"quarantined\":%u,\"native_status\":%u,\"before\":%u,\"after\":%u,\"config_writes\":%u,\"rollback_verified\":%u,\"native_sensor_mapping_readback\":%u,\"work_frame_bytes\":%u,\"raw16_stride_budget_bytes\":%u,\"power_scene_before\":%u,\"power_scene_after\":%u,\"native_power_scene_readback\":%u,\"power_scene_word\":%u,\"ipc_setup_before\":%u,\"ipc_setup_after\":%u,\"ipc_init_before\":%u,\"ipc_init_after\":%u,\"ipc_update_before\":%u,\"ipc_update_after\":%u,\"observed_ipc_init\":%u,\"observed_ipc_update\":%u,\"backend_selector_before\":%u,\"backend_selector_after\":%u,\"observed_backend_selector\":%u,\"ldc_recipe_policy_before\":%u,\"ldc_recipe_policy_after\":%u,\"observed_ldc_recipe_policy\":%u,\"observed_lcac_mask_instruction\":%u,\"observed_lcac_or_instruction\":%u,\"native_ipc_preset\":%u,\"replacement_blocks_before\":%u,\"replacement_blocks_after\":%u,\"recorded_full_width_4K_proven\":false}\n",ok?"true":"false",reason,name,rtos_entered,rtos_retired,ownership_verified?VFW_CAP_BYTES:0,cap_released,ui_thawed,quarantined,receipt.status,receipt.before,receipt.after,receipt.writes,receipt.rollback_verified,receipt.target_mode,receipt.work_frame,receipt.raw_dma_max_bytes,receipt.power_before,receipt.power_after,receipt.native_power_scene,receipt.power_scene_word,receipt.ipc_setup_before,receipt.ipc_setup_after,receipt.ipc_init_before,receipt.ipc_init_after,receipt.ipc_update_before,receipt.ipc_update_after,observed_route.init,observed_route.update,receipt.backend_before,receipt.backend_after,observed_route.backend,receipt.ldc_policy_before,receipt.ldc_policy_after,observed_route.ldc_policy,observed_route.lcac_mask,observed_route.lcac_or,observed.native_ipc_preset,receipt.block_before,receipt.block_after);fclose(f);}snprintf(p,sizeof p,"%s/native-receipt.bin",dir);f=fopen(p,"wx");if(f){fwrite(&receipt,1,sizeof receipt,f);fclose(f);}}
}
int main(int argc,char**argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_VIDEO_FULLWIDTH=R10j; NATIVE_DCI_UHD_FLOW_TIMING; TRACE_ONLY_OPERATION; NX1_LDC_UHD_BOUNDARY_3846_2168; NATIVE_HIGH_LOW_STAGE_TIMING; PAIRED_IPC_LDC_STREAM_POLICY; NATIVE_SETUP_BOUNDARY_SCALED_BACKEND; ORIGINAL_UD_METHODS_INTACT; NX500_1.12; PP19_RAW12_6496_3644_TO_NATIVE_3840_2160; NATIVE_MV_UD_POWER8; NATIVE_SCALER_E005; GUARDED_BOUNDARY; BOOT_RESIDENT_IDLE_TMC_LEASE; ATOMIC_ARM_BRANCH_ENTRIES; TRANSIENT_CAP_2M; NO_FLASH");return 0;}
 if(argc!=2||(strcmp(argv[1],"status")&&strcmp(argv[1],"apply")&&strcmp(argv[1],"restore")&&strcmp(argv[1],"trace")))return 2;
 int ok=0,queue=-1,mode_lock=-1,run_lock=-1,command_queued=0,request_started=0,pending_owned=0,worker_stable=0,operation=!strcmp(argv[1],"trace")?VFW_TRACE:!strcmp(argv[1],"apply")?VFW_APPLY:VFW_RESTORE,read_only=!strcmp(argv[1],"status");char dir[1024]={0},path[1200];struct alloc_regs regs;struct alloc_message message;started=ns();nonce=(uint32_t)started^(uint32_t)getpid()^VFW_MAGIC;if(!nonce)nonce=1;
 signal(SIGINT,stop);signal(SIGTERM,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);signal(SIGALRM,stop);alarm(35);
#define REQUIRE(c,m) do{if(!(c)){reason=(m);goto done;}}while(0)
 reason="NX500_FIRMWARE_IDENTITY_FAILED";REQUIRE(firmware()&&fingerprint("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app",MEM_APP_BYTES,MEM_APP_FNV),reason);
 pid=find_camera();REQUIRE(pid>1,"CAMERA_PROCESS_UNAVAILABLE");snprintf(path,sizeof path,"/proc/%d/mem",pid);procfd=open(path,(read_only?O_RDONLY:O_RDWR)|O_CLOEXEC);REQUIRE(procfd>=0,"CAMERA_PROCESS_READ_FAILED");
 /* Use a unique SD directory for immutable native trace / rollback evidence. */
 snprintf(dir,sizeof dir,"/mnt/mmc/NXDEMO51/VIDEO_FULL_%llu_%d_%08x",(unsigned long long)time(NULL),getpid(),nonce);REQUIRE(!mkdir(dir,0700),"VIDEO_AUDIT_DIRECTORY_FAILED");
 snprintf(path,sizeof path,"%s/probe.log",dir);log_file=fopen(path,"wx");snprintf(path,sizeof path,"%s/trace_events.txt",dir);trace_file=fopen(path,"wx");snprintf(path,sizeof path,"%s/native-commands.log",dir);command_log=fopen(path,"wx");REQUIRE(log_file&&trace_file&&command_log,"VIDEO_AUDIT_OPEN_FAILED");
 REQUIRE(load_maps(dir,"begin")&&app_guards()&&constructor(),"CAMERA_CONTEXT_OR_OUTPUT_UNKNOWN");devfd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);REQUIRE(devfd>=0&&code_guards()&&vfw_capture_route(physical_read,NULL,&observed,&observed_route),"FULLWIDTH_ORIGINAL_CODE_OR_PROFILE_UNREADABLE");
 REQUIRE(snapshot_file(dir,"before-profile.bin"),"FULLWIDTH_PROFILE_AUDIT_SAVE_FAILED");
 if(!read_only)REQUIRE(tmc_snapshot_file(dir),"NATIVE_TIMING_PREFLIGHT_SAVE_FAILED");
 int initial=vfw_route_mode(&observed,&observed_route);REQUIRE(initial&&vfw_budget(&observed),"FULLWIDTH_PROFILE_OR_NATIVE_BUDGET_UNKNOWN");
 if(read_only){ok=1;reason=initial==VFW_FULL?"FULLWIDTH_ARMED_RESELECT_NATIVE_UHD":(initial==VFW_LEGACY_FULL||initial==VFW_LEGACY_SCALE||initial==VFW_LEGACY_UPDATE||initial==VFW_LEGACY_LDC||initial==VFW_LEGACY_LCAC||initial==VFW_LEGACY_WIDTH||initial==VFW_LEGACY_BOUNDARY)?"LEGACY_FULLWIDTH_PIPELINE_REAPPLY_REQUIRED":"NATIVE_CROP_OR_QHD_CONFIGURATION";goto done;}
 (void)mkdir("/tmp/nx-lut-lite",0700);mode_lock=lockfile("/tmp/nx-lut-lite/video-mode.fd");run_lock=lockfile("/mnt/mmc/NXDEMO51/run.lock");struct stat st;REQUIRE(mode_lock>=0&&run_lock>=0&&lstat("/tmp/nx-lut-lite/engine.lock",&st)<0&&errno==ENOENT,"VIDEO_MODE_OR_SRW_TASK_BUSY");
 REQUIRE(lstat("/tmp/nx-lut-lite/video-fullwidth.pending",&st)<0&&errno==ENOENT,"PREVIOUS_VIDEO_TRANSACTION_UNCONFIRMED_RESTART_REQUIRED");
 if((operation==VFW_TRACE&&observed_route.blocks&&(initial==VFW_FULL||initial==VFW_STOCK))||(operation!=VFW_TRACE&&initial==(operation==VFW_APPLY?VFW_FULL:VFW_STOCK))){ok=1;reason=initial==VFW_FULL?"FULLWIDTH_ALREADY_ARMED":"NATIVE_CONFIGURATION_ALREADY_RESTORED";goto done;}
 REQUIRE(operation!=VFW_APPLY||constructor()==VM_UHD,"RESTORE_3840_UHD_OUTPUT_FIRST");REQUIRE(quiet_source(),idle_reason);REQUIRE(slot_original(),"NATIVE_ATTRIBUTE_DISPATCH_BUSY");
 REQUIRE(fingerprint("/usr/lib/libcapture-fw-prod.so",ALLOC_CAP_BYTES,ALLOC_CAP_FNV)&&fingerprint("/usr/lib/libshell-command.so",ALLOC_SHELL_BYTES,ALLOC_SHELL_FNV)&&fingerprint("/lib/libc-2.13.so",ALLOC_LIBC_BYTES,ALLOC_LIBC_FNV)&&fingerprint("/usr/lib/libudd5.so",ALLOC_UDD_BYTES,ALLOC_UDD_FNV)&&guards(cap_base,alloc_cap_guards,sizeof alloc_cap_guards/sizeof*alloc_cap_guards)&&guards(shell_base,alloc_shell_guards,sizeof alloc_shell_guards/sizeof*alloc_shell_guards)&&guards(libc_base,alloc_libc_guards,sizeof alloc_libc_guards/sizeof*alloc_libc_guards)&&bridge_context(),"NATIVE_LIBRARIES_OR_CONSOLE_CONTEXT_CHANGED");
 REQUIRE(ownership_baseline(dir),video_allocator_reason);queue=queue_id();REQUIRE(queue>=0,"NATIVE_SHELL_QUEUE_BUSY");tid=find_worker(queue,dir);REQUIRE(tid>0,"NATIVE_SHELL_WORKER_UNAVAILABLE");patches[0].address=shell_base+ALLOC_ENTRY;patches[1].address=shell_base+ALLOC_RETURN;
 {int f=open("/tmp/nx-lut-lite/video-fullwidth.pending",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);REQUIRE(f>=0,"VIDEO_TRANSACTION_INTENT_CREATE_FAILED");pending_owned=1;int saved=dprintf(f,"NX_VIDEO_PENDING1\npid=%d\nnonce=%08x\n",getpid(),nonce)>0&&!fsync(f);if(close(f))saved=0;REQUIRE(saved,"VIDEO_TRANSACTION_INTENT_SAVE_FAILED");}
 REQUIRE(read_word(patches[0].address,&patches[0].original)&&patches[0].original==0xe92d4008u&&read_word(patches[1].address,&patches[1].original)&&patches[1].original==0xe8bd85f8u&&watchdog_start(),"NATIVE_CALL_BOUNDARY_UNPROVEN");
 REQUIRE(!ptrace(PTRACE_ATTACH,tid,0,0),"NATIVE_WORKER_ATTACH_FAILED");attached=1;REQUIRE(wait_stop(3000,SIGSTOP)&&other_threads_running()&&verify_parked(dir,"parked",queue,0)&&arm_patch(0),"NATIVE_WORKER_NOT_PARKED");alloc_make_message(&message,nonce);uint32_t request[5]={0,0,VFW_CAP_BYTES,2,0};memcpy(message.args[2],request,20);
 REQUIRE(queue_id()==queue&&watchdog_mode('H'),"NATIVE_MESSAGE_CONTEXT_CHANGED");if(msgsnd(queue,&message,sizeof message-4,IPC_NOWAIT)){(void)watchdog_mode('V');REQUIRE(0,"NATIVE_MESSAGE_QUEUE_FAILED");}command_queued=1;REQUIRE(resume()&&wait_stop(5000,SIGTRAP)&&verify_breakpoint(0)&&get_regs(&regs)&&own_command(&message,&regs),"NATIVE_OWN_MESSAGE_UNCONFIRMED");
 entry_intercepted=call_boundary_active=1;call_original=regs;call_original.r[15]=shell_base+ALLOC_ENTRY;struct alloc_regs skip=call_original;skip.r[0]=0;skip.r[15]=shell_base+ALLOC_RETURN;skip.r[17]=UINT32_MAX;REQUIRE(set_regs(&skip),"NATIVE_COMMAND_SKIP_FAILED");command_queued=0;REQUIRE(watchdog_mode('V')&&restore_patch(0)&&arm_patch(1),"NATIVE_CALL_PREPARATION_FAILED");
 REQUIRE(pwrite(procfd,request,20,(off_t)(receive_buffer+48))==20,"TRANSIENT_REQUEST_WRITE_FAILED");request_started=1;REQUIRE(invoke(cap_base+ALLOC_REQUEST,receive_buffer+48,8000)&&get_regs(&regs)&&!regs.r[0]&&read_process(receive_buffer+48,response,20),"TRANSIENT_REQUEST_RETURN_UNKNOWN");request_done=1;
 REQUIRE(load_maps(dir,"allocated")&&confirm_owned()&&quiet_source()&&setup_alias(),"TRANSIENT_CAP_OWNERSHIP_OR_ALIAS_UNPROVEN");((struct vfw_context*)(expected+VFW_CONTEXT))->operation=(uint32_t)operation;for(unsigned i=0;i<64;i++)cap_alias[VFW_CONTEXT+i]=expected[VFW_CONTEXT+i];video_publish_barrier();
 REQUIRE(console_command(P33_MON_STATUS,dir,"native-before.txt"),"ORIGINAL_CONSOLE_BEFORE_FAILED");REQUIRE(cache_packet(nonce^1u,VFW_SLOT&~4095u,4096u)&&console_command(P33_MON_STATUS,dir,"slot-preclean-order.txt"),"NATIVE_SLOT_PRECLEAN_UNCONFIRMED");REQUIRE(quiet_source()&&publish_slot(),"NATIVE_SLOT_PUBLISH_FAILED");
 REQUIRE(cache_packet(nonce^2u,VFW_SLOT&~4095u,4096u)&&cache_packet(nonce,response[1]+VFW_CONTEXT,(uint32_t)operation),"NATIVE_COMMAND_SEND_UNCONFIRMED");REQUIRE(read_reply(1,3000)&&receipt.slot_restored&&slot_original(),"RTOS_READY_NOT_RECEIVED");rtos_entered=1;
 /* Calls into Linux libraries are complete before other threads stop. The
  * RTOS block waits in uncached owned memory, exactly as the Still bridge. */
 REQUIRE(freeze_ui(),"CAMERA_UI_FREEZE_UNCONFIRMED");for(unsigned i=0;i<5;i++){REQUIRE(quiet_source(),"CAMERA_CHANGED_BEFORE_FULLWIDTH_COMMIT");pause_ms(20);}
 video_publish_barrier();*(volatile uint32_t*)(cap_alias+VFW_CONTROL)=nonce^VFW_GO_XOR;video_publish_barrier();REQUIRE(read_reply(2,3000),"RTOS_TRANSACTION_COMPLETION_UNCONFIRMED");REQUIRE(thaw_ui(),"CAMERA_UI_DETACH_UNCONFIRMED");
 REQUIRE(console_command(P33_MON_STATUS,dir,"native-retired.txt")&&slot_original(),"RTOS_RETIREMENT_UNCONFIRMED");rtos_retired=1;REQUIRE(return_cap(dir),"TRANSIENT_CAP_RETURN_UNCONFIRMED");REQUIRE(!receipt.status&&receipt.after==(operation==VFW_TRACE?(uint32_t)initial:operation==VFW_APPLY?VFW_FULL:VFW_STOCK)&&receipt.target_mode==(receipt.after==VFW_FULL?23u:25u)&&receipt.native_power_scene==(receipt.after==VFW_FULL?8u:9u),receipt.status==VFW_TMC_BUSY?"NATIVE_TIMING_COUNT_FACILITY_BUSY":receipt.status==VFW_TMC_UNSTABLE?"NATIVE_TIMING_STATE_CHANGED":"FULLWIDTH_NATIVE_TRANSACTION_REJECTED");
 ok=1;reason=operation==VFW_TRACE?"FLOW_TIMING_ARMED_OUTPUT_UNCHANGED":operation==VFW_APPLY?"FULLWIDTH_APPLIED_RESELECT_NATIVE_UHD":"NATIVE_UHD_SENSOR_PROFILE_RESTORED";
done:
 alarm(0);sig_atomic_t saved_stop=stop_requested;stop_requested=0;
 if(cap_alias&&slot_published&&!rtos_retired){*(volatile uint32_t*)(cap_alias+VFW_CONTROL+4)=~nonce;video_publish_barrier();if(!api_inflight&&read_reply(2,1000)&&receipt.slot_restored)rtos_entered=1;}
 if(!ui_thawed&&!thaw_ui()){ok=0;reason="CAMERA_UI_DETACH_UNCONFIRMED";}
 if(slot_map&&slot_published&&!rtos_retired&&!api_inflight&&trace_stopped&&ui_thawed&&slot_map[0]==response[1]&&!slot_map[1]){slot_map[0]=VFW_ORIGINAL;video_publish_barrier();(void)cache_packet(nonce^3u,VFW_SLOT&~4095u,4096u);}
 if(ownership_verified&&slot_published&&!rtos_retired&&!api_inflight&&trace_stopped&&slot_original()&&ui_thawed&&console_command(P33_MON_STATUS,dir,"native-retired-recovery.txt")){rtos_retired=1;}
 /* A never-published owned block has no RTOS references. After publication,
  * retirement must be observed before release, including cancellation. */
 if(ownership_verified&&!slot_published)rtos_retired=1;
 if(ownership_verified&&!cap_released&&!release_done&&!api_inflight&&trace_stopped&&rtos_retired&&ui_thawed)(void)return_cap(dir);
 if(api_inflight||command_queued||(slot_published&&!rtos_retired)||(receipt.writes&&receipt.status&&!receipt.rollback_verified)||!ui_thawed){quarantined=1;ok=0;if(pid>1)kill(pid,SIGSTOP);reason="VIDEO_NATIVE_STATE_UNCONFIRMED_RESTART_REQUIRED";}
 if((attached||watch_pid>0)&&!cleanup_trace()){ok=0;reason="VIDEO_NATIVE_TRACE_CLEANUP_UNCONFIRMED";}
 if(tid&&detached_ok&&!quarantined&&!api_inflight){worker_stable=stabilize_worker(dir,queue);if(!worker_stable){ok=0;reason="NATIVE_WORKER_IDLE_RETURN_UNCONFIRMED";}}
 if(pending_owned&&!quarantined&&!api_inflight&&ui_thawed&&!command_queued&&(!tid||worker_stable||(!native_calls&&!attached&&!patches[0].armed&&!patches[1].armed))&&(!request_started||(request_done&&!response[0]&&!response[1])||cap_released)&&(!slot_published||rtos_retired))if(unlink("/tmp/nx-lut-lite/video-fullwidth.pending")){ok=0;reason="VIDEO_TRANSACTION_INTENT_CLEANUP_FAILED";}
 if(cap_alias){munmap((void*)cap_alias,VFW_CAP_BYTES);cap_alias=NULL;}if(console_map)munmap((void*)console_map,P33_CONSOLE_BYTES);if(slot_map)munmap((void*)((uintptr_t)slot_map&~4095u),4096);if(sma_fd>=0)close(sma_fd);if(rwfd>=0)close(rwfd);
 if(ownership_verified&&!cap_released){ok=0;if(!quarantined)reason="TRANSIENT_CAP_RETURN_UNCONFIRMED";}
 int state=devfd>=0&&vfw_capture_route(physical_read,NULL,&observed,&observed_route)?vfw_route_mode(&observed,&observed_route):VFW_UNKNOWN;if(dir[0]&&log_file)(void)snapshot_file(dir,"after-profile.bin");result(ok,state,dir);if(command_log)fclose(command_log);if(trace_file)fclose(trace_file);if(log_file)fclose(log_file);if(manager_map)munmap((void*)manager_map,0xb2000);if(root_map)munmap((void*)root_map,4096);if(devfd>=0)close(devfd);if(procfd>=0)close(procfd);if(run_lock>=0)close(run_lock);if(mode_lock>=0)close(mode_lock);free(baseline);free(baseline_again);stop_requested=saved_stop;return ok?0:1;
}
