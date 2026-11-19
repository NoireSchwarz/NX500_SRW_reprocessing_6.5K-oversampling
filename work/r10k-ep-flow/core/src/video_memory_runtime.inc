/* R11e: reuse only the disabled CAP-snapshot arena inside our existing RAM file.
 * The outer capture owns the mapping; no independent allocation or unmap. */
struct dm_runtime {struct dm_state state;void*allocation;uint32_t*data;size_t allocation_bytes;const struct vp_reader*reader;};
static uint32_t dm_free_kb(void){
 char text[4096];if(!read_text("/proc/meminfo",text,sizeof text))return 0;
 const char*p=strstr(text,"MemFree:");unsigned long v;
 return p&&sscanf(p,"MemFree: %lu kB",&v)==1&&v<=UINT32_MAX?(uint32_t)v:0;
}
static void dm_cache_info(struct dm_state*d){
 unsigned unified=0;
 for(unsigned i=0;i<8;i++){
  char path[160],text[128];snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu0/cache/index%u/type",i);
  if(!read_text(path,text,sizeof text))continue;text[strcspn(text,"\r\n")]=0;
  if(strcmp(text,"Data")&&strcmp(text,"Unified"))continue;
  snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu0/cache/index%u/size",i);
  if(!read_text(path,text,sizeof text)){d->cache_incomplete=1;continue;}
  unsigned n;char unit=0,extra=0;
  if(sscanf(text,"%u%c %c",&n,&unit,&extra)!=2||(unit!='K'&&unit!='M')||n>8192u){d->cache_incomplete=1;continue;}
  uint64_t bytes=(uint64_t)n*(unit=='K'?1024u:1048576u);
  if(bytes>UINT32_MAX-d->cache_bytes){d->cache_incomplete=1;continue;}
  d->cache_bytes+=(uint32_t)bytes;d->cache_entries++;snprintf(path,sizeof path,"/sys/devices/system/cpu/cpu0/cache/index%u/type",i);if(read_text(path,text,sizeof text)&&!strncmp(text,"Unified",7))unified=1;
 }
 if(!d->cache_entries||!unified)d->cache_incomplete=1;
}
static void dm_pagemap(struct dm_runtime*m){
 struct dm_state*d=&m->state;int f=open("/proc/self/pagemap",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);if(f<0)return;
 uint32_t hash=2166136261u;unsigned pages=DM_BYTES/4096u;int ok=1;
 for(unsigned i=0;i<pages;i+=16u){
  uint64_t v[16];off_t off=(off_t)(((uint64_t)(uintptr_t)m->data/4096u+i)*8u);
  if(pread(f,v,sizeof v,off)!=(ssize_t)sizeof v){ok=0;break;}
  for(unsigned j=0;j<16;j++){
   uint64_t pfn=v[j]&0x7fffffffffffffULL;
   if(!(v[j]>>63)||v[j]&(1ULL<<62)||!pfn||pfn>0xfffffu){ok=0;break;}
   uint32_t pa=(uint32_t)pfn*4096u;d->present_pages++;if(pa<d->PA_min)d->PA_min=pa;if(pa>d->PA_max)d->PA_max=pa;
   hash=bv_hash_more(hash,&pa,4);unsigned ordinal=i+j;
   if(ordinal%(pages/8u)==0)d->PA_samples[ordinal/(pages/8u)]=pa;
  }
  if(!ok)break;
 }
 if(close(f))ok=0;d->PFN_hash=hash;d->pagemap_status=ok&&d->present_pages==pages?1u:2u;
}
static void dm_runtime_release(struct dm_runtime*m){
 m->allocation=NULL;m->data=NULL;m->allocation_bytes=0;
}
static void dm_runtime_init(struct dm_runtime*m,unsigned seed,void*map,const struct bv_header*h){
 memset(m,0,sizeof*m);dm_init(&m->state,seed,0);struct dm_state*d=&m->state;
 d->initial_free_kb=dm_free_kb();dm_cache_info(d);
 if(!map||h->w[12]){d->reason=DM_ARENA_IN_USE;return;}
 if(d->initial_free_kb<DM_ADMISSION_KB){d->reason=DM_LOW_MEMORY;return;}
 m->allocation=(unsigned char*)map+DM_ARENA_GUARD;
 m->allocation_bytes=DM_BYTES+8192u;
 m->data=(uint32_t*)((unsigned char*)map+DM_ARENA_DATA);
 for(unsigned i=0;i<DM_BYTES/4u;i++)m->data[i]=(i*2654435761u)^0x7251c9a3u;
 if(mprotect(m->allocation,4096u,PROT_NONE)||
    mprotect((unsigned char*)m->data+DM_BYTES,4096u,PROT_NONE)||
    mprotect(m->data,DM_BYTES,PROT_READ)){
  dm_runtime_release(m);d->reason=DM_NO_ROOM;return;
 }
 d->ready_flags=15u;dm_pagemap(m);d->free_kb=dm_free_kb();
 if(d->free_kb<DM_ADMISSION_KB){dm_runtime_release(m);d->reason=DM_LOW_MEMORY;return;}
 d->status=DM_READY;
}
static int dm_admission_file(const struct vp_header*h,const struct dm_state*d){
 int dir=open(h->report,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0)return 0;
 int fd=openat(dir,"ADMISSION",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
 char b[768];int n=snprintf(b,sizeof b,
 "NXLMEMADMISSION2\nrevision=R11e MEMORY_ARENA\npid=%u\ncookie=%u\n"
 "status=%u\nreason=%u\ninitial_MemFree_kB=%u\npost_setup_MemFree_kB=%u\n"
 "working_set_bytes=%u\nextra_allocation_bytes=0\nallocation=REUSED_INACTIVE_CAPTURE_ARENA\n"
 "arena_data_offset=%u\narena_end_offset=%u\ncapture_RAM_bytes=%u\n"
 "admission_min_kB=%u\nrunning_min_kB=8192\nreadonly_flags=%u\n",
 h->pid,h->cookie,d->status,d->reason,d->initial_free_kb,d->free_kb,
 DM_BYTES,(unsigned)DM_ARENA_DATA,(unsigned)DM_ARENA_END,(unsigned)BV_END,DM_ADMISSION_KB,d->ready_flags);
 int ok=fd>=0&&n>0&&n<(int)sizeof b&&all_write(fd,b,(size_t)n)&&!fsync(fd);
 if(fd>=0&&close(fd))ok=0;if(close(dir))ok=0;return ok;
}
static uint64_t dm_now(void*p){(void)p;return clock_ns(CLOCK_MONOTONIC);}
static uint64_t dm_cpu(void*p){(void)p;return clock_ns(CLOCK_PROCESS_CPUTIME_ID);}
static uint32_t dm_batch(void*p,unsigned large,unsigned iterations){
 struct dm_runtime*m=p;return dm_read32(m->data,iterations,(large?DM_BYTES:DM_SMALL)-1u,&m->state.cursor);
}
static int dm_stop(void*p){(void)p;return interrupted!=0;}


static uint32_t dm_native_after(void*p){struct dm_runtime*m=p;uint32_t raw[16];return m->reader&&pl_read_header(m->reader,raw)&&vp_timer_schema(raw)==11u?raw[2]:UINT32_MAX;}
