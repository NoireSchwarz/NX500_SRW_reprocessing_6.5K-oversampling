/* SPDX-License-Identifier: AGPL-3.0-only. All readers are passive. Record
 * begin/end times, unavailable values and sampling gaps; no atomicity claim.
 * Change suppression preserves sequence ranges, with a100ms hot heartbeat.
 */
#define HOT_COUNT (sizeof ram_addresses/sizeof *ram_addresses)
#define CONTEXT_COUNT (sizeof context_pages/sizeof *context_pages)
_Static_assert(HOT_COUNT+6<=96u,"hot availability bitmap");
#include "passive_profile/combined_core.h"
static const uint32_t context_pages[]={
 0x813eb000u,0x813ed000u,0x813ee000u,0x813ef000u,0x813f0000u,0x813f1000u,
 0x81550000u,0x8156c000u,0x8156d000u,0x8178f000u,0x81790000u,0x81791000u,
 0x81792000u,0x817d8000u,0x8153e000u,0x8153f000u,0x81540000u,0x81541000u,
 0x81543000u,0x8153c000u,0x8142d000u,0x8142e000u,0x8142a000u,
 0x81453000u,0x817d7000u,0x817e0000u,0x86755000u,0x8671d000u
};
static FILE *hot_file,*context_file,*allocator_file,*linux_file;
static unsigned char file_buffers[4][65536];
static uint32_t context_previous[CONTEXT_COUNT][1024],context_seen[CONTEXT_COUNT];
static uint32_t context_status[CONTEXT_COUNT];
static unsigned context_cursor,hot_pending,hot_written;
static uint32_t hot_last[HOT_COUNT+6],hot_mask[3];
static uint64_t hot_last_time,hot_max_gap,hot_records,hot_samples;
static uint64_t dropped_frames;static int output_error;
static const uint32_t repeated_keys[6]={0x8153f3f8u,0x81540f38u,0x8142de78u,0x867555c4u,0x86755238u,0x867554c4u};
static int process_mem=-1;static uint32_t udd_base,linux_old_crc,ui_old_crc;
static uint64_t linux_last,ui_last;
static uint32_t typed_previous[2+P36_WORDS];static unsigned typed_seen;
static uint64_t typed_last_ns;
static int observer_path(const char*path){
 const char*p="/mnt/mmc/NXDEMO51/INIT_LENS_";size_t n=strlen(p);if(strncmp(path,p,n)||!path[n]||strlen(path)>1023)return 0;
 for(const char*q=path+n;*q;q++)if(!((*q>='a'&&*q<='z')||(*q>='A'&&*q<='Z')||(*q>='0'&&*q<='9')||*q=='_'||*q=='-'))return 0;return 1;
}
#include "lens-photo-inventory.inc"
static uint32_t crc32(const void *data,size_t n){
 static uint32_t table[256];static int ready;
 if(!ready){for(unsigned i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);table[i]=c;}ready=1;}
 const unsigned char *p=data;uint32_t c=~0u;while(n--)c=table[(c^*p++)&255u]^(c>>8);return ~c;
}
static int frame(FILE *f,unsigned kind,unsigned sequence,uint64_t begin,uint64_t end,const void *data,unsigned bytes){
 if(!f||ftello(f)<0||(uint64_t)ftello(f)+40u+bytes>64u*1024u*1024u){dropped_frames++;stop_requested=1;output_error=1;return 0;}
 uint32_t header[10]={0x3152584eu,kind,sequence,(uint32_t)begin,(uint32_t)(begin>>32),(uint32_t)end,(uint32_t)(end>>32),bytes,crc32(data,bytes),0};header[9]=crc32(header,36);
 int ok=fwrite(header,1,sizeof header,f)==sizeof header&&fwrite(data,1,bytes,f)==bytes;if(!ok){dropped_frames++;stop_requested=1;output_error=1;}return ok;
}
static int block(uint32_t address,uint32_t*out,unsigned words){return words&&words<=1024&&mapped_read(address,out,words*4u,0);}
static void maps_sample(unsigned sample){
 (void)sample;
 char path[80],text[262144];snprintf(path,sizeof path,"/proc/%d/maps",camera);if(!read_text(path,text,sizeof text))return;
 fprintf(events,"{\"event\":\"maps\",\"mono_ns\":%llu,\"possibly_truncated\":%s,\"text\":",(unsigned long long)now(),strlen(text)==sizeof text-1?"true":"false");quoted(events,text);fputs("}\n",events);
 for(char *p=text;*p;){char *end=strchr(p,'\n');if(end)*end=0;unsigned lo,hi,off;char perms[8],device[24],name[256];unsigned long inode;
  if(sscanf(p,"%x-%x %7s %x %23s %lu %255s",&lo,&hi,perms,&off,device,&inode,name)==7&&!off&&!strcmp(name,"/usr/lib/libudd5.so")&&perms[0]=='r'&&perms[2]=='x')udd_base=lo;
  if(!end)break;p=end+1;
 }
}
static int typed_reader(void*ctx,uint32_t a,uint32_t*out,unsigned words){(void)ctx;return words&&words<=1024&&mapped_read(a,out,words*4u,1);}
#include "video-front-capture.inc"
#include "lens_follow.inc"
#include "lens_flow_capture.inc"
#include "lens_persistent_capture.inc"
#include "lens-control-graph.inc"
#include "native-pixel-capture.inc"
#include "ldc-hardware-capture.inc"
static void typed_sample(unsigned sequence){
 uint64_t begin=now();uint32_t state=0,ep_state=0,selected=0;
 int ok=word(P34_TASK+3364u,&state)&&word(P35_EP+3364u,&ep_state)&&word(P35_EP+3444u,&selected);
 int active=ok&&((state&255u)!=7u||((ep_state&255u)>=2u&&(ep_state&255u)<=4u&&selected==P35_STILL));
 if(!active&&typed_seen&&begin-typed_last_ns<1000000000u)return;
 uint32_t data[2+P36_WORDS];struct p36_snapshot *snap=(struct p36_snapshot*)(data+2);
 data[0]=(uint32_t)p36_capture(typed_reader,NULL,1,snap);data[1]=P36_WORDS;typed_last_ns=begin;
 if((int32_t)data[0]==1){lens_follow_sample(sequence,snap);native_pixel_sample(sequence,snap);}
 if(!typed_seen||memcmp(data,typed_previous,sizeof data)){
  frame(context_file,5,sequence,begin,now(),data,sizeof data);memcpy(typed_previous,data,sizeof data);typed_seen=1;
 }
}
#include "native-gamma-capture.inc"
#include "lens-schema.inc"
#include "udd-identity.h"
static int lens_loaded_identity(void){
 if(process_mem<0||!udd_base)return 0;unsigned char code[32];
 for(unsigned i=0;i<sizeof lens_udd_guards/sizeof *lens_udd_guards;i++)if(pread(process_mem,code,32,(off_t)(uint64_t)(udd_base+lens_udd_guards[i].offset))!=32||memcmp(code,lens_udd_guards[i].code,32))return 0;return 1;
}
static int broad_begin(void){
 hot_file=new_file("hot.bin");context_file=new_file("contexts.bin");allocator_file=new_file("allocator.bin");linux_file=new_file("linux-context.bin");
 if(!hot_file||!context_file||!allocator_file||!linux_file)return 0;
 FILE *files[4]={hot_file,context_file,allocator_file,linux_file};for(unsigned i=0;i<4;i++)setvbuf(files[i],(char*)file_buffers[i],_IOFBF,sizeof file_buffers[i]);
 char path[80];snprintf(path,sizeof path,"/proc/%d/mem",camera);process_mem=open(path,O_RDONLY|O_CLOEXEC);
 /* Lens mode does not allocate or scan the CAP table. */
 FILE *schema=new_file("schema.json");if(!schema)return 0;
 schema_write(schema);
 FILE*hw_schema=new_file("ldc-hardware-schema.json");if(!hw_schema)return 0;lens_ldc_hardware_schema(hw_schema);int hw_saved=durable(hw_schema);if(fclose(hw_schema)||!hw_saved)return 0;
 FILE*graph_schema=new_file("ldc-control-schema.json");if(!graph_schema)return 0;
 fprintf(graph_schema,"{\"format\":\"NX_LDC_CONTROL_GRAPH1\",\"kind\":11,\"bytes\":%u,\"members\":{",(unsigned)sizeof(struct lsg_sample));
#define GRAPH_LAYOUT(m) fprintf(graph_schema,"\"" #m "\":[%u,%u],",(unsigned)offsetof(struct lsg_sample,m),(unsigned)sizeof(((struct lsg_sample*)0)->m))
 GRAPH_LAYOUT(schema);GRAPH_LAYOUT(status);GRAPH_LAYOUT(read_mask);GRAPH_LAYOUT(guards_before);GRAPH_LAYOUT(guards_after);GRAPH_LAYOUT(controls);GRAPH_LAYOUT(owners_before);GRAPH_LAYOUT(owners_after);
#undef GRAPH_LAYOUT
 fprintf(graph_schema,"\"config\":[%u,%u]},\"statuses\":{\"1\":\"guarded read complete; not atomic\",\"2\":\"unconstructed/unknown type\",\"3\":\"read unavailable\",\"4\":\"roots changed\",\"5\":\"owner outside bounded ordinary RAM\"},\"native_calls\":0,\"device_writes\":0,\"image_pointer_following\":false}\n",(unsigned)offsetof(struct lsg_sample,config),(unsigned)sizeof(((struct lsg_sample*)0)->config));
 int graph_saved=durable(graph_schema);if(fclose(graph_schema)||!graph_saved)return 0;
 FILE*gamma_schema=new_file("native-gamma-schema.json");if(!gamma_schema)return 0;
 native_gamma_schema(gamma_schema);
 int gamma_saved=durable(gamma_schema);if(fclose(gamma_schema)||!gamma_saved)return 0;
 FILE*video_schema=new_file("video-front-schema.json");if(!video_schema)return 0;video_front_schema(video_schema);int video_ok=durable(video_schema);if(fclose(video_schema)||!video_ok)return 0;
 video_schema=new_file("video-sensor-schema.json");if(!video_schema)return 0;video_sensor_schema(video_schema);video_ok=durable(video_schema);if(fclose(video_schema)||!video_ok)return 0;
 video_schema=new_file("video-window-schema.json");if(!video_schema)return 0;video_geometry_schema(video_schema);video_ok=durable(video_schema);if(fclose(video_schema)||!video_ok)return 0;
 video_schema=new_file("video-runtime-schema.json");if(!video_schema)return 0;video_runtime_schema(video_schema);video_ok=durable(video_schema);if(fclose(video_schema)||!video_ok)return 0;
 int ok=durable(schema);if(fclose(schema))ok=0;maps_sample(0);return ok&&lens_loaded_identity();
}
static void hot_sample(unsigned sequence){
 uint32_t values[HOT_COUNT+6],mask[3]={0,0,0};uint64_t begin=now();
 if(hot_last_time&&begin-hot_last_time>hot_max_gap)hot_max_gap=begin-hot_last_time;hot_last_time=begin;hot_samples++;
 for(unsigned i=0;i<HOT_COUNT;i++){values[i]=0;if(!word(ram_addresses[i],values+i))mask[i/32]|=1u<<(i%32);}
 for(unsigned i=0;i<6;i++){values[HOT_COUNT+i]=0;if(!word(repeated_keys[i],values+HOT_COUNT+i))mask[(HOT_COUNT+i)/32]|=1u<<((HOT_COUNT+i)%32);}
 hot_pending++;
 if(!hot_written||memcmp(values,hot_last,sizeof values)||memcmp(mask,hot_mask,sizeof mask)||hot_pending>=50){
  uint32_t data[4+HOT_COUNT+6];data[0]=hot_pending-1;memcpy(data+1,mask,sizeof mask);memcpy(data+4,values,sizeof values);
  if(frame(hot_file,1,sequence,begin,now(),data,sizeof data))hot_records++;
  memcpy(hot_last,values,sizeof values);memcpy(hot_mask,mask,sizeof mask);hot_written=1;hot_pending=0;
 }
}
static void context_sample(unsigned sequence){
 unsigned i=context_cursor++%CONTEXT_COUNT;uint32_t data[1026];uint64_t begin=now();data[0]=context_pages[i];data[1]=(unsigned)block(data[0],data+2,1024);
 if(!context_seen[i]||context_status[i]!=data[1]||memcmp(context_previous[i],data+2,4096)){
  frame(context_file,2,sequence,begin,now(),data,sizeof data);memcpy(context_previous[i],data+2,4096);context_seen[i]=1;context_status[i]=data[1];
 }
}
static void ui_wait_sample(unsigned sequence){
 char path[96],call[512]={0},channel[160]={0};snprintf(path,sizeof path,"/proc/%d/task/%d/syscall",camera,camera);read_text(path,call,sizeof call);
 snprintf(path,sizeof path,"/proc/%d/task/%d/wchan",camera,camera);read_text(path,channel,sizeof channel);
 uint32_t h=hash_bytes(call,strlen(call))^hash_bytes(channel,strlen(channel));uint64_t stamp=now();
 if(ui_last&&h==ui_old_crc&&stamp-ui_last<1000000000u)return;ui_last=stamp;ui_old_crc=h;
 fprintf(events,"{\"event\":\"ui_wait\",\"sequence\":%u,\"mono_ns\":%llu,\"syscall\":",sequence,(unsigned long long)stamp);quoted(events,call);fputs(",\"wchan\":",events);quoted(events,channel);fputs("}\n",events);
 blocked_sample(camera,call,sequence);
}
static void blocked_sample(int tid,const char *text,unsigned sequence){
 if(process_mem<0)return;long nr;unsigned long a[6],sp,pc;
 if(sscanf(text,"%ld %lx %lx %lx %lx %lx %lx %lx %lx",&nr,a,a+1,a+2,a+3,a+4,a+5,&sp,&pc)!=9)return;
 /* nr0 is ARM restart_syscall, not ioctl. Preserve its stack and PC as
  * evidence, without assigning a driver call number to it. */
 if(nr<0||nr>1024)return;
 uint64_t begin=now();
 if(sp>=4096u&&(uint64_t)sp+2048u<=0xc0000000ULL){
  uint32_t data[6+512];ssize_t n=pread(process_mem,data+6,2048,(off_t)(uint64_t)sp);
  data[0]=(uint32_t)tid;data[1]=(uint32_t)nr;data[2]=(uint32_t)sp;data[3]=(uint32_t)pc;data[4]=2048;data[5]=n>0?(uint32_t)n:0;
  static struct {int tid;uint32_t hash;uint64_t at;} prior[128];
  unsigned slot=(unsigned)tid%128u;uint32_t fingerprint=crc32(data,24u+data[5]);
  if(prior[slot].tid!=tid||prior[slot].hash!=fingerprint||begin-prior[slot].at>=1000000000u){
   frame(linux_file,6,sequence,begin,now(),data,24u+data[5]);
   prior[slot].tid=tid;prior[slot].hash=fingerprint;prior[slot].at=begin;
  }
 }
 if(nr==54){
  unsigned bytes=(a[1]>>16)&0x3fffu;if(!bytes||bytes>4096u||a[2]<4096u||(uint64_t)a[2]+bytes>0xc0000000ULL)return;
  char path[96],target[160];snprintf(path,sizeof path,"/proc/%d/fd/%lu",camera,a[0]);ssize_t n=readlink(path,target,sizeof target-1);if(n<0)return;target[n]=0;
  if(strcmp(target,"/dev/drime5_ep")&&strcmp(target,"/dev/drime5_ipcm")&&strcmp(target,"/dev/d5_sma"))return;
  unsigned char packet[4096];n=pread(process_mem,packet,bytes,(off_t)(uint64_t)a[2]);
  fprintf(events,"{\"event\":\"driver_wait_packet\",\"sequence\":%u,\"mono_ns\":%llu,\"tid\":%d,\"command\":%lu,\"fd_target\":",sequence,(unsigned long long)now(),tid,a[1]);quoted(events,target);
  fprintf(events,",\"pointer\":%lu,\"requested_bytes\":%u,\"read_bytes\":%d,\"hex\":\"",a[2],bytes,(int)n);for(ssize_t i=0;i<n;i++)fprintf(events,"%02x",packet[i]);fputs("\"}\n",events);
 }
}
/* RAM shadow locations verified by replay of the pinned original setters.
 * They are cached configurations, NOT live MMIO readback or write traces. */
static void lens_shadow_sample(unsigned sequence){
 if(process_mem<0||!udd_base)return;
 uint64_t begin=now();uint32_t data[100]={udd_base,0,0x55f80u,96u};
 data[1]=pread(process_mem,data+4,384,(off_t)(uint64_t)(udd_base+0x55f80u))==384;
 static uint32_t previous[100];static uint64_t last;
 if(!last||memcmp(previous,data,sizeof data)||begin-last>=1000000000u){
  frame(linux_file,7,sequence,begin,now(),data,sizeof data);memcpy(previous,data,sizeof data);last=begin;
 }
}
static void linux_sample(unsigned sequence){
 /* High rate shadow capture is performed next to lens_flow_sample. */
 if(process_mem<0)return;uint64_t begin=now();uint32_t data[83]={0};data[0]=udd_base;
 const uint32_t offsets[4]={0x55560u,0x55d90u,0x57080u,0x57720u},counts[4]={36,16,8,16};unsigned at=2;
 for(unsigned i=0;i<4;i++){if(udd_base&&pread(process_mem,data+at,counts[i]*4u,(off_t)(uint64_t)(udd_base+offsets[i]))==(ssize_t)(counts[i]*4u))data[1]|=1u<<i;at+=counts[i];}
 const uint32_t app[4]={0x0044b480u,0x004aa52cu,0x004aa2f0u,0x004aa258u};
 for(unsigned i=0;i<4;i++)if(pread(process_mem,data+at+i,4,(off_t)(uint64_t)app[i])==4)data[1]|=1u<<(i+4);
 uint32_t crc=crc32(data,sizeof data);if(!linux_last||crc!=linux_old_crc||begin-linux_last>=1000000000u){frame(linux_file,4,sequence,begin,now(),data,sizeof data);linux_old_crc=crc;linux_last=begin;}
}
static int broad_flush(void){
 video_front_summary();lens_flow_summary();lens_persistent_summary();lens_graph_summary();native_gamma_summary();
 fprintf(events,"{\"event\":\"sampling_tick\",\"mono_ns\":%llu,\"hot_samples\":%llu,\"hot_records\":%llu,\"max_hot_gap_us\":%llu,\"dropped_frames\":%llu}\n",(unsigned long long)now(),(unsigned long long)hot_samples,(unsigned long long)hot_records,(unsigned long long)(hot_max_gap/1000),(unsigned long long)dropped_frames);
 return durable(hot_file)&&durable(context_file)&&durable(allocator_file)&&durable(linux_file);
}
static void broad_end(void){
 fprintf(events,"{\"event\":\"sampling_summary\",\"hot_samples\":%llu,\"hot_records\":%llu,\"max_hot_gap_us\":%llu,\"uncommitted_repeat_samples\":%u,\"atomic\":false}\n",(unsigned long long)hot_samples,(unsigned long long)hot_records,(unsigned long long)(hot_max_gap/1000),hot_pending);
 broad_flush();fclose(hot_file);fclose(context_file);fclose(allocator_file);fclose(linux_file);if(process_mem>=0)close(process_mem);
}
