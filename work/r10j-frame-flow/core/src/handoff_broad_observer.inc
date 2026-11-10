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
static uint64_t dropped_frames;
static const uint32_t repeated_keys[6]={0x8153f3f8u,0x81540f38u,0x8142de78u,0x867555c4u,0x86755238u,0x867554c4u};
static unsigned char *allocator_data;
static unsigned allocator_at,allocator_active,allocator_seen,allocator_available,allocator_head_changed;
static uint32_t allocator_before,allocator_old_crc;
static uint32_t allocator_old_meta[4];
static uint64_t allocator_begin_ns,allocator_next;
static int process_mem=-1;static uint32_t udd_base,linux_old_crc,ui_old_crc;
static uint64_t linux_last,ui_last;
static uint32_t typed_previous[2+P36_WORDS];static unsigned typed_seen;
static uint64_t typed_last_ns;
static int observer_path(const char *path){
 const char *p="/mnt/mmc/NXDEMO51/";size_t n=strlen(p);
 if(strncmp(path,p,n)||strstr(path,".."))return 0;
 if(strncmp(path+n,"JOB_",4)&&strncmp(path+n,"INIT_",5))return 0;
 for(const char *q=path+n;*q;q++)if(!((*q>='A'&&*q<='Z')||(*q>='a'&&*q<='z')||(*q>='0'&&*q<='9')||*q=='_'||*q=='-'||*q=='/'))return 0;
 return 1;
}
static void photo_inventory(const char *phase){
 DIR *root=opendir("/mnt/mmc/DCIM");if(!root)return;struct dirent *folder;unsigned count=0;
 while((folder=readdir(root))&&count<4096){if(folder->d_name[0]=='.'||strchr(folder->d_name,'/'))continue;
  struct stat sd;if(fstatat(dirfd(root),folder->d_name,&sd,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(sd.st_mode))continue;
  char path[384];if(snprintf(path,sizeof path,"/mnt/mmc/DCIM/%s",folder->d_name)>=(int)sizeof path)continue;
  DIR *d=opendir(path);if(!d)continue;struct dirent *entry;
  while((entry=readdir(d))&&count<4096){if(entry->d_name[0]=='.'||strchr(entry->d_name,'/'))continue;
   struct stat st;if(fstatat(dirfd(d),entry->d_name,&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode))continue;
   fprintf(events,"{\"event\":\"photo_inventory\",\"phase\":\"%s\",\"directory\":",phase);quoted(events,folder->d_name);fputs(",\"file\":",events);quoted(events,entry->d_name);
   fprintf(events,",\"bytes\":%llu,\"mtime\":%lld,\"body_opened\":false}\n",(unsigned long long)st.st_size,(long long)st.st_mtime);count++;
  }closedir(d);
 }closedir(root);
}
static uint32_t crc32(const void *data,size_t n){
 static uint32_t table[256];static int ready;
 if(!ready){for(unsigned i=0;i<256;i++){uint32_t c=i;for(unsigned k=0;k<8;k++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);table[i]=c;}ready=1;}
 const unsigned char *p=data;uint32_t c=~0u;while(n--)c=table[(c^*p++)&255u]^(c>>8);return ~c;
}
static int frame(FILE *f,unsigned kind,unsigned sequence,uint64_t begin,uint64_t end,const void *data,unsigned bytes){
 if(!f||ftello(f)>64*1024*1024){dropped_frames++;return 0;}
 uint32_t header[10]={0x3152584eu,kind,sequence,(uint32_t)begin,(uint32_t)(begin>>32),(uint32_t)end,(uint32_t)(end>>32),bytes,crc32(data,bytes),0};header[9]=crc32(header,36);
 return fwrite(header,1,sizeof header,f)==sizeof header&&fwrite(data,1,bytes,f)==bytes;
}
static int block(uint32_t address,uint32_t *out,unsigned words){
 int valid=1;for(unsigned i=0;i<words;){uint32_t ignored;if(!word(address+4*i,&ignored)){valid=0;out[i++]=0;continue;}
  uint32_t base=(address+4*i)&~4095u;unsigned p=0;while(p<page_count&&pages[p].base!=base)p++;
  unsigned n=(4096u-((address+4*i)&4095u))/4u;if(n>words-i)n=words-i;
  if(p==page_count)return 0;const volatile uint32_t *at=(const volatile uint32_t*)(pages[p].memory+((address+4*i)&4095u));
  for(unsigned k=0;k<n;k++)out[i+k]=at[k];i+=n;
 }return valid;
}
static void maps_sample(unsigned sample){
 if(sample%5)return;char path[80],text[262144];snprintf(path,sizeof path,"/proc/%d/maps",camera);if(!read_text(path,text,sizeof text))return;
 fprintf(events,"{\"event\":\"maps\",\"mono_ns\":%llu,\"possibly_truncated\":%s,\"text\":",(unsigned long long)now(),strlen(text)==sizeof text-1?"true":"false");quoted(events,text);fputs("}\n",events);
 for(char *p=text;*p;){char *end=strchr(p,'\n');if(end)*end=0;unsigned lo,hi,off;char perms[8],device[24],name[256];unsigned long inode;
  if(sscanf(p,"%x-%x %7s %x %23s %lu %255s",&lo,&hi,perms,&off,device,&inode,name)==7&&!off&&!strcmp(name,"/usr/lib/libudd5.so")&&perms[0]=='r'&&perms[2]=='x')udd_base=lo;
  if(!end)break;p=end+1;
 }
}
static int typed_reader(void *unused,uint32_t address,uint32_t *out,unsigned words){
 (void)unused;if(!words||words>1024||(address&3u))return 0;
 if(address>=0x81000000u&&(uint64_t)address+4u*words<=0x94000000u)return block(address,out,words);
 /* P35 follows only bounded native vtables and class-name/code prefixes.
  * This ROM range is read-only metadata, never an ISP register window. */
 if(dev<0||address<0x80000000u||(uint64_t)address+words*4u>0x80b84280u)return 0;
 for(unsigned i=0;i<words;i++){
  uint32_t at=address+4u*i,base=at&~4095u;unsigned p;for(p=0;p<page_count;p++)if(pages[p].base==base)break;
  if(p==page_count){if(page_count==256)return 0;void *map=mmap(NULL,4096,PROT_READ,MAP_SHARED,dev,(off_t)(uint64_t)base);if(map==MAP_FAILED)return 0;pages[p].base=base;pages[p].memory=map;page_count++;}
  out[i]=*(const volatile uint32_t*)(pages[p].memory+(at&4095u));
 }return 1;
}
static void typed_sample(unsigned sequence){
 uint64_t begin=now();uint32_t state=0,ep_state=0,selected=0;
 int ok=word(P34_TASK+3364u,&state)&&word(P35_EP+3364u,&ep_state)&&word(P35_EP+3444u,&selected);
 int active=ok&&((state&255u)!=7u||((ep_state&255u)>=2u&&(ep_state&255u)<=4u&&selected==P35_STILL));
 if(!active&&typed_seen&&begin-typed_last_ns<1000000000u)return;
 uint32_t data[2+P36_WORDS];struct p36_snapshot *snap=(struct p36_snapshot*)(data+2);
 data[0]=(uint32_t)p36_capture(typed_reader,NULL,1,snap);data[1]=P36_WORDS;typed_last_ns=begin;
 if(!typed_seen||memcmp(data,typed_previous,sizeof data)){
  frame(context_file,5,sequence,begin,now(),data,sizeof data);memcpy(typed_previous,data,sizeof data);typed_seen=1;
 }
}
static void schema_write(FILE *schema){
 fprintf(schema,"{\"format\":\"NXINITBROAD1\",\"interval_us\":4000,\"hot_heartbeat_us\":100000,\"ui_syscall_interval_us\":20000,\"linux_context_interval_us\":50000,\"context_page_interval_us\":20000,\"pages\":%u,\"frame_header_u32\":[\"magic\",\"kind\",\"sequence\",\"begin_ns_lo\",\"begin_ns_hi\",\"end_ns_lo\",\"end_ns_hi\",\"payload_bytes\",\"payload_crc32\",\"header_crc32\"],\"hot_payload\":\"skipped_equal_samples_before_current, unavailable_mask[3], fixed_words[N], repeated_key_words[6]\",\"hot_addresses\":[",(unsigned)CONTEXT_COUNT);
 for(unsigned i=0;i<HOT_COUNT;i++)fprintf(schema,"%s%u",i?",":"",ram_addresses[i]);
 fputs("],\"context_pages\":[",schema);for(unsigned i=0;i<CONTEXT_COUNT;i++)fprintf(schema,"%s%u",i?",":"",context_pages[i]);
 fputs("],\"repeat_key_addresses\":[",schema);for(unsigned i=0;i<6;i++)fprintf(schema,"%s%u",i?",":"",repeated_keys[i]);
 fputs("],\"linux_blocks\":[[349536,36],[351632,16],[356480,8],[358176,16]],\"linux_app_addresses\":[4502656,4891948,4891376,4891224],\"kinds\":{\"1\":\"hot\",\"2\":\"context: address,available,data[1024]\",\"3\":\"allocator: free_before,free_after,available,head_changed,raw_table\",\"4\":\"process: udd_base,validity_mask,block_values,app_values,reserved_zero\",\"5\":\"P36 typed snapshot: signed_return,word_count,raw_words; original readers under passive_profile\",\"6\":\"user stack: tid,syscall,sp,pc,requested_bytes,actual_bytes,raw_bytes\"},\"atomic\":false,\"pixel_reads\":0,\"device_writes\":0}\n",schema);
}
static int broad_begin(void){
 hot_file=new_file("hot.bin");context_file=new_file("contexts.bin");allocator_file=new_file("allocator.bin");linux_file=new_file("linux-context.bin");
 if(!hot_file||!context_file||!allocator_file||!linux_file)return 0;
 FILE *files[4]={hot_file,context_file,allocator_file,linux_file};for(unsigned i=0;i<4;i++)setvbuf(files[i],(char*)file_buffers[i],_IOFBF,sizeof file_buffers[i]);
 char path[80];snprintf(path,sizeof path,"/proc/%d/mem",camera);process_mem=open(path,O_RDONLY|O_CLOEXEC);
 allocator_data=malloc(45056u*16u);if(!allocator_data)return 0;
 FILE *schema=new_file("schema.json");if(!schema)return 0;
 schema_write(schema);
 int ok=durable(schema);if(fclose(schema))ok=0;maps_sample(0);return ok;
}
static void hot_sample(unsigned sequence){
 uint32_t values[HOT_COUNT+6],mask[3]={0,0,0};uint64_t begin=now();
 if(hot_last_time&&begin-hot_last_time>hot_max_gap)hot_max_gap=begin-hot_last_time;hot_last_time=begin;hot_samples++;
 for(unsigned i=0;i<HOT_COUNT;i++){values[i]=0;if(!word(ram_addresses[i],values+i))mask[i/32]|=1u<<(i%32);}
 for(unsigned i=0;i<6;i++){values[HOT_COUNT+i]=0;if(!word(repeated_keys[i],values+HOT_COUNT+i))mask[(HOT_COUNT+i)/32]|=1u<<((HOT_COUNT+i)%32);}
 hot_pending++;
 if(!hot_written||memcmp(values,hot_last,sizeof values)||memcmp(mask,hot_mask,sizeof mask)||hot_pending>=25){
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
static void allocator_sample(unsigned sequence){
 uint64_t stamp=now();uint32_t free_count=0;int valid=word(0x81eac910u,&free_count);
 if(!allocator_active){if(stamp<allocator_next)return;allocator_active=1;allocator_at=0;allocator_begin_ns=stamp;allocator_before=free_count;allocator_available=(unsigned)valid;allocator_head_changed=0;}
 if(!valid)allocator_available=0;else if(free_count!=allocator_before)allocator_head_changed=1;
 unsigned n=1024;if(n>45056u*4u-allocator_at)n=45056u*4u-allocator_at;
 if(!block(0x81dfc908u+allocator_at*4u,(uint32_t*)allocator_data+allocator_at,n))allocator_available=0;allocator_at+=n;
 if(allocator_at==45056u*4u){
  uint32_t meta[4]={allocator_before,free_count,allocator_available,allocator_head_changed};
  uint32_t crc=crc32(allocator_data,45056u*16u);if(!allocator_seen||crc!=allocator_old_crc||memcmp(meta,allocator_old_meta,sizeof meta)){
   uint32_t *data=malloc(16+45056u*16u);if(data){data[0]=allocator_before;data[1]=free_count;data[2]=allocator_available;data[3]=allocator_head_changed;memcpy(data+4,allocator_data,45056u*16u);frame(allocator_file,3,sequence,allocator_begin_ns,now(),data,16+45056u*16u);free(data);}
   allocator_old_crc=crc;memcpy(allocator_old_meta,meta,sizeof meta);allocator_seen=1;
  }allocator_active=0;allocator_next=stamp+2000000000u;
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
 if(nr!=54&&nr!=240&&nr!=117)return;
 uint64_t begin=now();
 if(sp>=4096u&&(uint64_t)sp+2048u<=0xc0000000ULL){
  uint32_t data[6+512];ssize_t n=pread(process_mem,data+6,2048,(off_t)(uint64_t)sp);
  data[0]=(uint32_t)tid;data[1]=(uint32_t)nr;data[2]=(uint32_t)sp;data[3]=(uint32_t)pc;data[4]=2048;data[5]=n>0?(uint32_t)n:0;
  frame(linux_file,6,sequence,begin,now(),data,24u+data[5]);
 }
 if(nr==54){
  unsigned bytes=(a[1]>>16)&0x3fffu;if(!bytes||bytes>128u||a[2]<4096u||(uint64_t)a[2]+bytes>0xc0000000ULL)return;
  char path[96],target[160];snprintf(path,sizeof path,"/proc/%d/fd/%lu",camera,a[0]);ssize_t n=readlink(path,target,sizeof target-1);if(n<0)return;target[n]=0;
  if(strcmp(target,"/dev/drime5_ep")&&strcmp(target,"/dev/d5_sma"))return;
  unsigned char packet[128];n=pread(process_mem,packet,bytes,(off_t)(uint64_t)a[2]);
  fprintf(events,"{\"event\":\"driver_wait_packet\",\"sequence\":%u,\"mono_ns\":%llu,\"tid\":%d,\"command\":%lu,\"fd_target\":",sequence,(unsigned long long)now(),tid,a[1]);quoted(events,target);
  fprintf(events,",\"pointer\":%lu,\"requested_bytes\":%u,\"read_bytes\":%d,\"hex\":\"",a[2],bytes,(int)n);for(ssize_t i=0;i<n;i++)fprintf(events,"%02x",packet[i]);fputs("\"}\n",events);
 }
}
static void linux_sample(unsigned sequence){
 if(process_mem<0)return;uint64_t begin=now();uint32_t data[83]={0};data[0]=udd_base;
 const uint32_t offsets[4]={0x55560u,0x55d90u,0x57080u,0x57720u},counts[4]={36,16,8,16};unsigned at=2;
 for(unsigned i=0;i<4;i++){if(udd_base&&pread(process_mem,data+at,counts[i]*4u,(off_t)(uint64_t)(udd_base+offsets[i]))==(ssize_t)(counts[i]*4u))data[1]|=1u<<i;at+=counts[i];}
 const uint32_t app[4]={0x0044b480u,0x004aa52cu,0x004aa2f0u,0x004aa258u};
 for(unsigned i=0;i<4;i++)if(pread(process_mem,data+at+i,4,(off_t)(uint64_t)app[i])==4)data[1]|=1u<<(i+4);
 uint32_t crc=crc32(data,sizeof data);if(!linux_last||crc!=linux_old_crc||begin-linux_last>=1000000000u){frame(linux_file,4,sequence,begin,now(),data,sizeof data);linux_old_crc=crc;linux_last=begin;}
}
static int broad_flush(void){
 fprintf(events,"{\"event\":\"sampling_tick\",\"mono_ns\":%llu,\"hot_samples\":%llu,\"hot_records\":%llu,\"max_hot_gap_us\":%llu,\"dropped_frames\":%llu}\n",(unsigned long long)now(),(unsigned long long)hot_samples,(unsigned long long)hot_records,(unsigned long long)(hot_max_gap/1000),(unsigned long long)dropped_frames);
 return durable(hot_file)&&durable(context_file)&&durable(allocator_file)&&durable(linux_file);
}
static void broad_end(void){
 fprintf(events,"{\"event\":\"sampling_summary\",\"hot_samples\":%llu,\"hot_records\":%llu,\"max_hot_gap_us\":%llu,\"uncommitted_repeat_samples\":%u,\"atomic\":false}\n",(unsigned long long)hot_samples,(unsigned long long)hot_records,(unsigned long long)(hot_max_gap/1000),hot_pending);
 broad_flush();fclose(hot_file);fclose(context_file);fclose(allocator_file);fclose(linux_file);if(process_mem>=0)close(process_mem);free(allocator_data);
}
