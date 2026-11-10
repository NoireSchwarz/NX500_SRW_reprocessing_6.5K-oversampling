/* NX500 1.12 native NTSC/UHD verification and UHD/DC Pro bitrate only.
 * The camera menu selects NTSC / UHD30; read its resolved shared UserSpec.
 * Its native /dev/mem PFN mapping is not ordinary /proc/PID/mem RAM.
 * Verify that mapping, then map only the first parameter page read-only.
 * No sensor timing, MP4 timestamps or firmware files are patched here.
 */
#define VP_MOVIE "/usr/lib/libmmutil_movie.so.0.0.0"
#define VP_USER "/usr/lib/libuser-spec.so"
#define VP_RATE_AT 0xa04u
#define VP_PREF_PHYSICAL 0x85440000u
#define VP_PREF_PAGE 4096u
static const uint32_t vp_rate70[2]={0x50ecf44fu,0x402cf2c0u};
static const uint32_t vp_rate80[2]={0x4000f24bu,0x40c4f2c0u};
/* Accept the exact previous R9q/R9r pair to migrate an already loaded 100M
 * slot; it is no longer a selectable target. All other code is pinned. */
static const uint32_t vp_rate100[2]={0x1000f24eu,0x50f5f2c0u};
static unsigned vp_preferences_valid,vp_rate,vp_ntsc,vp_size,vp_quality,vp_writes,vp_rollback,vp_thawed;
static uint32_t vp_pref_pointer,vp_pref_map_begin,vp_pref_map_end,vp_raw_preferences[3];
static int vp_pref_errno;
static const char*vp_preference_error="VIDEO_SETTINGS_ROOT_UNAVAILABLE";
static int vp_digest(const char*path,const char*expected){
 FILE*f=fopen(path,"rb");if(!f)return 0;struct sha256 h;sha256_init(&h);unsigned char b[32768],digest[32];size_t n;while((n=fread(b,1,sizeof b,f)))sha256_update(&h,b,n);int ok=!ferror(f);if(fclose(f))ok=0;char hash[65];sha256_final(&h,digest);digest_hex(digest,hash);return ok&&!strcmp(hash,expected);
}
static int vp_base(struct camera*c,const char*path,uint32_t*out){
 char name[100],line[512];snprintf(name,sizeof name,"/proc/%d/maps",c->pid);FILE*f=fopen(name,"r");if(!f)return 0;unsigned found=0;uint32_t base=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)==7&&!offset&&!strcmp(perms,"r-xp")&&!strcmp(file,path)&&lo>0x10000u&&hi>lo&&hi-lo<0x1000000u){base=lo;found++;}
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;if(ok)*out=base;return ok;
}
static int vp_rate_read(struct camera*c,uint32_t words[2]){return c->movie_base&&pread(c->mem,words,8,(off_t)(c->movie_base+VP_RATE_AT))==8;}
static int vp_rate_context(struct camera*c){
 if(!c->movie_base)return 1;unsigned char text[636];if(pread(c->mem,text,sizeof text,(off_t)(c->movie_base+0x818u))!=sizeof text)return 0;
 uint32_t current[2];memcpy(current,text+VP_RATE_AT-0x818u,8);if(memcmp(current,vp_rate70,8)&&memcmp(current,vp_rate80,8)&&memcmp(current,vp_rate100,8))return 0;
 memcpy(text+VP_RATE_AT-0x818u,vp_rate70,8);struct sha256 h;unsigned char digest[32];char hex[65];sha256_init(&h);sha256_update(&h,text,sizeof text);sha256_final(&h,digest);digest_hex(digest,hex);
 return !strcmp(hex,"8ce30b8dae3ae56e6aa7a0b1b25c75549f539124ca1b504bc2d974ca14f1f633");
}
static int vp_preference_mapping(struct camera*c,uint32_t pointer,uint32_t*begin,uint32_t*end){
 char name[100],line[512];snprintf(name,sizeof name,"/proc/%d/maps",c->pid);FILE*f=fopen(name,"r");if(!f)return 0;
 unsigned found=0;uint32_t b=0,e=0;
 while(fgets(line,sizeof line,f)){unsigned lo,hi,offset;char perms[8],dev[32],file[300];unsigned long inode;
  if(sscanf(line,"%x-%x %7s %x %31s %lu %299s",&lo,&hi,perms,&offset,dev,&inode,file)!=7)continue;
  if(strcmp(file,"/dev/mem")||strcmp(perms,"rw-s")||offset!=0x85400000u||lo<0x10000u||hi<=lo||(lo&4095u)||(hi&4095u))continue;
  /* Native pref_rtos_get_fd maps physical 0x85400000 for 0x80000 bytes;
   * get_virtual_address_j rounds that to 0x81000. UserSpec begins +0x40000.
   * Require the resolved native pointer, complete 38016-byte object and
   * exactly this native mapping, not an arbitrary /dev/mem alias. */
  if((uint64_t)hi-lo!=0x81000u||(uint64_t)lo+0x40000u!=pointer||(uint64_t)pointer+38016u>hi)continue;
  b=lo;e=hi;found++;
 }
 int ok=!ferror(f)&&found==1;if(fclose(f))ok=0;if(ok){*begin=b;*end=e;}return ok;
}
static int vp_preferences(struct camera*c){
 uint32_t location,pointer,location_after,pointer_after,a[3],b[3];
 vp_preferences_valid=0;vp_pref_errno=0;vp_preference_error="VIDEO_SETTINGS_ROOT_UNAVAILABLE";
 /* _m_us_data_info has an R_ARM_COPY relocation in di-camera-app. Follow
  * the resolved GOT entry, rather than the DSO's unused local BSS copy. */
 if(!c->user_base||pread(c->mem,&location,4,(off_t)(c->user_base+0x37330u))!=4||location!=0x49d0f8u||pread(c->mem,&pointer,4,(off_t)location)!=4||pointer<0x10000u||pointer>0xbffff000u||(pointer&3u))return 0;
 vp_pref_pointer=pointer;vp_preference_error="VIDEO_SETTINGS_MAPPING_UNAVAILABLE";
 uint32_t map_begin,map_end,map_begin_after,map_end_after;
 if(!vp_preference_mapping(c,pointer,&map_begin,&map_end))return 0;
 vp_pref_map_begin=map_begin;vp_pref_map_end=map_end;
 vp_preference_error="VIDEO_SETTINGS_PHYSICAL_READ_UNAVAILABLE";
 int fd=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(fd<0){vp_pref_errno=errno;return 0;}
 void*view=mmap(NULL,VP_PREF_PAGE,PROT_READ,MAP_SHARED,fd,(off_t)(uint64_t)VP_PREF_PHYSICAL);int saved=errno;
 if(close(fd)){vp_pref_errno=errno;if(view!=MAP_FAILED)(void)munmap(view,VP_PREF_PAGE);return 0;}
 if(view==MAP_FAILED){vp_pref_errno=saved;return 0;}
 volatile const uint32_t*words=view;
 static const unsigned offsets[3]={0x58u,0x250u,0x254u};
 __sync_synchronize();for(unsigned i=0;i<3;i++)a[i]=words[offsets[i]/4u];
 __sync_synchronize();for(unsigned i=0;i<3;i++)b[i]=words[offsets[i]/4u];__sync_synchronize();
 memcpy(vp_raw_preferences,a,sizeof a);
 if(munmap(view,VP_PREF_PAGE)){vp_pref_errno=errno;return 0;}
 vp_preference_error="VIDEO_SETTINGS_CHANGED_DURING_READ";
 if(pread(c->mem,&location_after,4,(off_t)(c->user_base+0x37330u))!=4||location_after!=location||pread(c->mem,&pointer_after,4,(off_t)location_after)!=4||pointer_after!=pointer||memcmp(a,b,sizeof a)||!vp_preference_mapping(c,pointer_after,&map_begin_after,&map_end_after)||map_begin_after!=map_begin||map_end_after!=map_end)return 0;
 vp_preference_error="VIDEO_SETTINGS_VALUE_OUT_OF_RANGE";if(a[0]>1||a[1]>14||a[2]>2)return 0;
 vp_ntsc=a[0]==0;vp_size=a[1];vp_quality=a[2];vp_preferences_valid=1;vp_preference_error="VIDEO_SETTINGS_READ_VERIFIED";return 1;
}
static int vp_library_context(struct camera*c,const char**why){
 *why="VIDEO_NATIVE_MOVIE_DISK_IDENTITY_FAILED";if(!vp_digest(VP_MOVIE,"269f7c69ad4448bfe01f71789132996df703b6ccecdff2a0089fd2bc9503ef3d"))return 0;
 *why="VIDEO_NATIVE_USER_DISK_IDENTITY_FAILED";if(!vp_digest(VP_USER,"2d04ab487cc9980cb87e513b25ff09011839203756d28857eedfd50a9cc730ec"))return 0;
 *why="VIDEO_NATIVE_MOVIE_MAPPING_UNAVAILABLE";if(!vp_base(c,VP_MOVIE,&c->movie_base))return 0;
 *why="VIDEO_NATIVE_USER_MAPPING_UNAVAILABLE";if(!vp_base(c,VP_USER,&c->user_base))return 0;
 *why="VIDEO_BITRATE_CONTEXT_CHANGED";if(!vp_rate_context(c))return 0;
 if(!vp_preferences(c)){*why=vp_preference_error;return 0;}return 1;
}
static void vp_log(struct camera*c,const char*phase,const char*why){
 int f=open("/mnt/mmc/NXDEMO51/logs/video-profile.log",O_WRONLY|O_CREAT|O_APPEND|O_NOFOLLOW|O_CLOEXEC,0600);if(f>=0){dprintf(f,"VIDEO_PROFILE pid=%d start=%llu phase=%s reason=%s preferences_valid=%u NTSC=%u movie_size=%u quality=%u UHD_DC_Pro_Mbps=%u final_chroma_requested=420 movie_base=%08x user_base=%08x preference_pointer=%08x preference_mapping=%08x-%08x preference_physical=%08x preference_read=RO_SHARED_PAGE preference_errno=%d preference_raw=%u,%u,%u preference_reason=%s\n",c->pid,(unsigned long long)c->start,phase,why,vp_preferences_valid,vp_ntsc,vp_size,vp_quality,vp_rate,c->movie_base,c->user_base,vp_pref_pointer,vp_pref_map_begin,vp_pref_map_end,VP_PREF_PHYSICAL,vp_pref_errno,vp_raw_preferences[0],vp_raw_preferences[1],vp_raw_preferences[2],vp_preference_error);close(f);}
}
static int vp_patch_rate(struct camera*c,unsigned target,const char**why){
 uint32_t before[2],after[2];int ok=0;
 *why="VIDEO_BITRATE_TARGET_INVALID";if(target!=70&&target!=80)return 0;
 const uint32_t*want=target==80?vp_rate80:vp_rate70;
 *why="VIDEO_BITRATE_CONTEXT_CHANGED";if(!vp_rate_context(c)||!vp_rate_read(c,before))return 0;
 if(!memcmp(before,want,8)){vp_rate=target;return 1;}
 *why="STOP_RECORDING_OR_TASK_BEFORE_CHANGE";if(!ready(c))return 0;
 *why="CAMERA_THREADS_NOT_QUIESCENT";if(!freeze(c))goto out;
 *why="VIDEO_BITRATE_CONTEXT_CHANGED";if(!ready(c)||!vp_rate_read(c,after)||memcmp(before,after,8))goto out;
 *why="VIDEO_BITRATE_WRITE_FAILED";
 vp_writes++;
 if(!write_word(c,c->movie_base+VP_RATE_AT,want[0])||!(vp_writes++,write_word(c,c->movie_base+VP_RATE_AT+4,want[1]))||!vp_rate_read(c,after)||memcmp(after,want,8)){
  int a=write_word(c,c->movie_base+VP_RATE_AT,before[0]),b=write_word(c,c->movie_base+VP_RATE_AT+4,before[1]);
  vp_rollback=a&&b&&vp_rate_read(c,after)&&!memcmp(after,before,8);
  *why=vp_rollback?"VIDEO_BITRATE_FAILED_RESTORED":"VIDEO_BITRATE_ROLLBACK_UNCONFIRMED_RESTART_REQUIRED";goto out;
 }
 vp_rate=target;ok=1;
out:
 vp_thawed=thaw(c);if(!vp_thawed){*why="CAMERA_THREAD_DETACH_UNCONFIRMED";ok=0;}return ok;
}
static int vp_profile(struct camera*c,const char*verb,const char**why){
 if(!vp_library_context(c,why))return 0;
 uint32_t old[2];if(!vp_rate_read(c,old))return 0;unsigned old_rate=!memcmp(old,vp_rate100,8)?100u:!memcmp(old,vp_rate80,8)?80u:70u;vp_rate=old_rate;vp_log(c,"BEFORE",verb);
 *why="STOP_RECORDING_OR_TASK_BEFORE_CHANGE";if(!ready(c))return 0;
 if(strcmp(verb,"30p")){int ok=vp_patch_rate(c,70,why);if(ok)*why="NATIVE_UHD_DC_PRO_70MBPS_RESTORED";vp_log(c,"AFTER",*why);return ok;}
 /* The firmware's sys_param handler is a factory-script endpoint. The
  * normal st/app shell has no equivalent video settings command. Require
  * the native selection; do not launch dfmsd or modify opaque preferences. */
 *why="SELECT_NTSC_UHD30_IN_CAMERA_MENU";
 if(!vp_ntsc||vp_size!=1){vp_log(c,"NATIVE_SELECTION_REQUIRED",*why);return 0;}
 if(!vp_patch_rate(c,80,why))return 0;
 *why="VIDEO_30P_SELECTION_CHANGED";if(!vp_preferences(c)){*why=vp_preference_error;vp_log(c,"SELECTION_CHANGED",*why);return 0;}if(!vp_ntsc||vp_size!=1){vp_log(c,"SELECTION_CHANGED",*why);return 0;}
 *why="NATIVE_UHD30_2997_PRO80_CONFIGURED";vp_log(c,"AFTER",*why);return 1;
}
