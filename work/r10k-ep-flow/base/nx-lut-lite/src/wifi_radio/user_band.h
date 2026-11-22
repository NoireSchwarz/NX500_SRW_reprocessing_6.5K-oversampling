/* The original getter's GOT resolves the main executable's R_ARM_COPY word.
 * Only that original variable and the OEM RTOS preference mapping are accepted.
 * The exact preference page is reserved DRAM, not an arbitrary MMIO window.
 */
struct band_word{uint32_t base,root,address,physical,value,root_variable;int device;};
#define US_RTOS_PHYSICAL 0x85400000u
#define US_RTOS_BYTES 0x81000u
#define US_COMMON_OFFSET 0x40000u
#define US_BAND_PHYSICAL (US_RTOS_PHYSICAL+US_COMMON_OFFSET+0x68u)
static int rtos_preference_mapping(pid_t pid,uint32_t root){char path[80],line[512];snprintf(path,sizeof path,"/proc/%ld/maps",(long)pid);FILE*f=fopen(path,"r");if(!f)return 0;unsigned found=0;while(fgets(line,sizeof line,f)){unsigned lo,hi,off;char perms[8],dev[32],file[300];unsigned long ino;if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&off,dev,&ino,file)==7&&!strcmp(file,"/dev/mem")&&!strcmp(perms,"rw-s")&&off==US_RTOS_PHYSICAL&&hi>lo&&(uint64_t)hi-lo==US_RTOS_BYTES&&(uint64_t)lo+US_COMMON_OFFSET==root)found++;}int ok=!ferror(f)&&found==1;fclose(f);return ok;}
static int app_copy_mapped(pid_t pid){char p[80],line[512];snprintf(p,sizeof p,"/proc/%ld/maps",(long)pid);FILE*f=fopen(p,"r");if(!f)return 0;unsigned found=0;while(fgets(line,sizeof line,f)){unsigned lo,hi,off;char perms[8],dev[32],file[300];unsigned long ino;if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&off,dev,&ino,file)==7&&!strcmp(perms,"rw-p")&&!strcmp(file,HOT_APP)&&lo<=US_ROOT_APP_COPY&&(uint64_t)US_ROOT_APP_COPY+4<=hi)found++;}int ok=!ferror(f)&&found==1;fclose(f);return ok;}
static int band_maps(pid_t pid,struct band_word*w,int find_library){
 char p[80],line[512];snprintf(p,sizeof p,"/proc/%ld/maps",(long)pid);FILE*f=fopen(p,"r");if(!f)return 0;unsigned found=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,off;char perms[8],dev[32],path[300]="";unsigned long ino;int fields=sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&off,dev,&ino,path);if(fields<6||hi<=lo)continue;
  if(find_library){if(fields==7&&!strcmp(path,"/usr/lib/libuser-spec.so")&&!strcmp(perms,"r-xp")&&!off&&(uint64_t)hi-lo>=0x2e000u){w->base=lo;found++;}}
  else if(w->address>=lo&&(uint64_t)w->address+4<=hi){
   if(fields==7&&!strcmp(path,"/dev/mem")&&!strcmp(perms,"rw-s")){uint64_t physical=(uint64_t)off+w->address-lo;if(off==US_RTOS_PHYSICAL&&(uint64_t)lo+US_COMMON_OFFSET==w->root&&(uint64_t)hi-lo==US_RTOS_BYTES&&physical==US_BAND_PHYSICAL&&rtos_preference_mapping(pid,w->root)){w->physical=(uint32_t)physical;w->device=1;found++;}}
  }
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;return ok;
}
static int band_device(struct band_word*w,int write,int expected,int wanted){
 if(w->physical!=US_BAND_PHYSICAL||!rtos_preference_mapping(trial.camera,w->root))return 0;int f=open("/dev/mem",(write?O_RDWR:O_RDONLY)|O_SYNC|O_CLOEXEC|O_NOFOLLOW);if(f<0)return 0;void*p=mmap(NULL,4096,PROT_READ|(write?PROT_WRITE:0),MAP_SHARED,f,(off_t)(uint64_t)(w->physical&~4095u));int closed=close(f);if(p==MAP_FAILED)return 0;if(closed){munmap(p,4096);return 0;}
 volatile uint32_t*q=(volatile uint32_t*)((unsigned char*)p+(w->physical&4095u));__sync_synchronize();uint32_t a=*q;__sync_synchronize();uint32_t b=*q;char raw[140];snprintf(raw,sizeof raw,"BAND_WORD_RAW a=%u b=%u write=%d",a,b,write);note(raw);int ok=a==b&&a<=1;
 if(ok&&write){ok=(int)a==expected;if(ok){*q=(uint32_t)wanted;__sync_synchronize();ok=*q==(uint32_t)wanted;}}
 if(ok)w->value=write?(uint32_t)wanted:a;if(munmap(p,4096))ok=0;return ok;
}
static int band_restore_gate(const struct hot_state*s){
 if(trial.phase!=P_RESTORE||!trial.band_dirty||trial.old_band!=0)return 0;
 if(!s->state&&!s->owned&&!s->phase)return 1;
 if(s->state!=10||!s->owned||s->phase!=3||s->callback!=0xc9194u)return 0;
 if(!trial.actors[0].pid||!trial.actors[1].pid)return 0;
 if(module_mask()!=0||interface_present("mlan0")!=0||interface_present("uap0")!=0||interface_present("p2p0")!=0||!loaders_idle())return 0;
 for(unsigned i=0;i<4;i++){const struct actor*a=trial.actors+i;if(a->pid&&(!a->frozen||!actor_match(a)||actor_state(a->pid)!='T'))return 0;}
 return 1;
}
static int band_native_gate(int restoring){struct hot_state s;if(!hot_snapshot(&s,0)||s.pid!=trial.camera||s.tick!=trial.camera_tick)return 0;return restoring?band_restore_gate(&s):!s.state&&!s.owned&&!s.phase;}
static int band_snapshot_mode(struct band_word*w,int restoring){
 memset(w,0,sizeof*w);struct hot_state s;if(!hot_snapshot(&s,0)||s.pid!=trial.camera||s.tick!=trial.camera_tick||(restoring?!band_restore_gate(&s):(s.state||s.owned||s.phase)))return failed("NATIVE_NETWORK_NOT_IDLE");
 if(!band_maps(s.pid,w,1))return failed("US_LIBRARY_MAPPING_MISSING");char path[80];snprintf(path,sizeof path,"/proc/%ld/mem",(long)s.pid);int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return failed("US_PROCESS_UNREADABLE");
 unsigned char code[32];uint32_t actual_rootvar=0,root=0,again=0,got_again=0;int ok=hot_read(fd,w->base+US_GET_POINT_GUARD_AT,code,sizeof us_get_point_guard)&&!memcmp(code,us_get_point_guard,sizeof us_get_point_guard)&&hot_read(fd,w->base+US_VALUE_GUARD_AT,code,sizeof us_value_guard)&&!memcmp(code,us_value_guard,sizeof us_value_guard)&&hot_read(fd,w->base+US_ROOT_INIT_AT,code,sizeof us_root_init_guard)&&!memcmp(code,us_root_init_guard,sizeof us_root_init_guard)&&hot_read(fd,w->base+US_RTOS_MAP_AT,code,sizeof us_rtos_map_guard)&&!memcmp(code,us_rtos_map_guard,sizeof us_rtos_map_guard)&&hot_read(fd,w->base+US_ROOT_GOT,&actual_rootvar,4)&&actual_rootvar==US_ROOT_APP_COPY&&app_copy_mapped(s.pid)&&hot_read(fd,actual_rootvar,&root,4)&&root>=0x10000u&&root<0xff000000u-0x6cu&&!(root&3u);
 char pointers[200];snprintf(pointers,sizeof pointers,"US_POINTERS valid=%d base=%08x variable=%08x root=%08x",ok,w->base,actual_rootvar,root);note(pointers);
 if(ok){w->root=root;w->address=root+0x68u;w->root_variable=actual_rootvar;ok=band_maps(s.pid,w,0);snprintf(pointers,sizeof pointers,"US_TARGET_MAP valid=%d address=%08x device=%d physical=%08x",ok,w->address,w->device,w->physical);note(pointers);}
 if(ok)ok=w->device&&band_device(w,0,0,0);
 if(ok)ok=hot_read(fd,actual_rootvar,&again,4)&&again==root&&hot_read(fd,w->base+US_ROOT_GOT,&got_again,4)&&got_again==actual_rootvar&&proc_ticks(s.pid)==s.tick;close(fd);if(!ok)return failed("US_BAND_SNAPSHOT_FAILED");
 struct band_word after=*w;if(!band_maps(s.pid,&after,1)||after.base!=w->base||!band_maps(s.pid,&after,0)||after.device!=w->device||after.physical!=w->physical)return failed("US_BAND_MAPPING_CHANGED");
 if(!band_native_gate(restoring))return failed("NATIVE_BAND_ACCESS_CHANGED");
 char b[240];snprintf(b,sizeof b,"BAND_READ value=%u root_variable=%08x root=%08x address=%08x device=%d physical=%08x",w->value,w->root_variable,w->root,w->address,w->device,w->physical);note(b);return 1;
}
static int band_exchange_mode(int expected,int wanted,int restoring){
 if(restoring&&(expected!=1||wanted!=0||!band_native_gate(1)))return failed("BAND_RESTORE_LEASE_REQUIRED");
 struct band_word w;if(!band_snapshot_mode(&w,restoring)||w.value!=(uint32_t)expected)return failed("US_BAND_VALUE_CHANGED");if(w.root!=trial.us_root||w.address!=trial.us_address||w.root_variable!=trial.us_root_variable)return failed("US_BAND_ROOT_CHANGED");
 if(!band_native_gate(restoring))return failed("NATIVE_BAND_WRITE_CHANGED");
 int ok=w.device&&band_device(&w,1,expected,wanted);
 if(!ok)return failed("US_BAND_WRITE_FAILED");struct band_word after;if(!band_snapshot_mode(&after,restoring)||after.root!=w.root||after.address!=w.address||after.value!=(uint32_t)wanted)return failed("US_BAND_READBACK_FAILED");note("BAND_WRITE_VERIFIED");return 1;
}

static int band_snapshot(struct band_word*w){return band_snapshot_mode(w,0);}
static int band_exchange(int expected,int wanted){return band_exchange_mode(expected,wanted,0);}
static int band_snapshot_restore(struct band_word*w){return band_snapshot_mode(w,1);}
static int band_restore(void){return band_exchange_mode(1,0,1);}
