/* SPDX-License-Identifier: AGPL-3.0-only. Selected NX500 source, no native calls.
 * DCIM and its one child directory are opened without following links. The
 * OEM /mnt/mmc mount alias is retained. Never interpolate a path in a shell.
 */
#include "srw3.h"
#include "../../common/lens_profile.h"
#include "../../common/srw_tone_curve.h"
#include <time.h>
#include "../../common/source_ticket.h"
static FILE *selected_file;
static struct stat selected_stat;
static char selected_path[512],selected_jpeg[512],selected_sha[65],selected_jpeg_sha[65];
static char selected_date_original[20];
static unsigned char selected_c0[2048];
static uint32_t selected_iq[256];
static uint64_t selected_jpeg_bytes;
static struct nj_config selected_jpeg_config;
static struct lp_config selected_lens_config;
static struct stc_source selected_tone_curve;
static unsigned selected_tone_identity;
static const char *selected_settings_error="SOURCE_ASSET_PREFLIGHT_FAILED";
static unsigned selected_query_mode,selected_source_ticket_used,selected_native_jpeg_hashed;
static uint64_t selected_hash_ns,selected_settings_ns,selected_pair_ns;
static uint64_t selected_mono(void){struct timespec t;return !clock_gettime(CLOCK_MONOTONIC,&t)?(uint64_t)t.tv_sec*1000000000u+t.tv_nsec:0;}
static int selected_header_read(void*v,uint64_t at,void*p,unsigned n){FILE*f=v;return !fseeko(f,(off_t)at,SEEK_SET)&&fread(p,1,n,f)==n;}
static int selected_component(const char *p,size_t n){
 if(!n||n>120||p[0]=='.')return 0;
 for(size_t i=0;i<n;i++){unsigned c=(unsigned char)p[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;}
 return 1;
}
static int selected_same(const struct stat *a,const struct stat *b){
 return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
 a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
 a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int selected_hash(FILE *f,char out[65]){
 unsigned char block[65536],digest[32];struct sha256 s;sha256_init(&s);
 if(fseeko(f,0,SEEK_SET))return 0;size_t n;
 while((n=fread(block,1,sizeof block,f))){if(stop_requested)return 0;sha256_update(&s,block,n);}
 if(ferror(f))return 0;sha256_final(&s,digest);digest_hex(digest,out);return !fseeko(f,0,SEEK_SET);
}
static void selected_close(void){if(selected_file)fclose(selected_file);selected_file=NULL;}
static int selected_unchanged(FILE *f){struct stat st;return f&&!fstat(fileno(f),&st)&&selected_same(&selected_stat,&st);}
static int selected_date(struct p41_reader *r,uint32_t tiff,uint64_t hi,char out[20]){
 unsigned char h[8];struct p41_entry e;uint32_t v[1];
 if(!p41_read(r,tiff,h,8,tiff,hi)||!((h[0]=='I'&&h[1]=='I')||(h[0]=='M'&&h[1]=='M')))return 0;
 r->le=h[0]=='I';if(p41_u16(h+2,r->le)!=42)return 0;
 uint64_t root=(uint64_t)tiff+p41_u32(h+4,r->le);if(root>0xffffffffu||!p41_find(r,(uint32_t)root,tiff,tiff,hi,0x8769,&e)||!p41_values(r,&e,4,1,tiff,hi,v))return 0;
 uint64_t exif=(uint64_t)tiff+v[0];if(exif>0xffffffffu||!p41_find(r,(uint32_t)exif,tiff,tiff,hi,0x9003,&e)||e.type!=2||e.count!=20||!p41_read(r,e.at,out,20,tiff,hi)||out[19])return 0;
 for(unsigned i=0;i<19;i++){unsigned c=(unsigned char)out[i];if(i==4||i==7||i==13||i==16){if(c!=':')return 0;}else if(i==10){if(c!=' ')return 0;}else if(c<'0'||c>'9')return 0;}return 1;
}
static int selected_jpeg_date(FILE *f,uint64_t size,char out[20]){
 struct p41_reader r={f,size,0,0};unsigned char b[10];uint64_t at=2;
 for(unsigned count=0;count<128&&at+4<=size&&at<1048576u;count++){
  if(!p41_read(&r,at,b,4,0,size)||b[0]!=255||b[1]==0xda||b[1]==0xd9)return 0;
  unsigned n=(unsigned)b[2]*256u+b[3];if(n<2||n>size-at-2u)return 0;
  if(b[1]==0xe1&&n>=16&&p41_read(&r,at+4,b,6,0,size)&&!memcmp(b,"Exif\0\0",6))return selected_date(&r,(uint32_t)(at+10),at+2+n,out);
  at+=2+n;
 }return 0;
}
static int selected_prepare(const char *path,const char *expected_hash){
 const char *prefix="/mnt/mmc/DCIM/";size_t pn=strlen(prefix);int root=-1,folder=-1,fd=-1,jfd=-1,ok=0;FILE *jf=NULL;
 struct stat jpg_before,jpg_after;struct srw_info raw;const char *why="";
 selected_close();p48_selected_ready=0;selected_settings_error="SOURCE_ASSET_PREFLIGHT_FAILED";memset(&selected_tone_curve,0,sizeof selected_tone_curve);selected_tone_identity=0;selected_sha[0]=0;selected_source_ticket_used=selected_native_jpeg_hashed=0;
 if(!selected_query_mode)nxl_live_progress("PIPE_SOURCE_VERIFY","RUNNING","Verifying source SRW and reading original JPEG / lens settings");
 if(!path||strlen(path)>=sizeof selected_path||strncmp(path,prefix,pn))goto done;
 const char *slash=strchr(path+pn,'/');if(!slash||strchr(slash+1,'/')||!selected_component(path+pn,(size_t)(slash-path-pn))||!selected_component(slash+1,strlen(slash+1)))goto done;
 size_t n=strlen(slash+1);if(n<5||strcasecmp(slash+1+n-4,".SRW"))goto done;
 char dir[121],jpg[125];memcpy(dir,path+pn,(size_t)(slash-path-pn));dir[slash-path-pn]=0;
 memcpy(jpg,slash+1,n-4);memcpy(jpg+n-4,".JPG",5);
 root=open("/mnt/mmc/DCIM",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(root<0)goto done;
 folder=openat(root,dir,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(folder<0)goto done;
 fd=openat(folder,slash+1,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0)goto done;
 if(fstat(fd,&selected_stat)||!S_ISREG(selected_stat.st_mode)||selected_stat.st_size<1048576||selected_stat.st_size>134217728)goto done;
 selected_file=fdopen(fd,"rb");if(!selected_file)goto done;fd=-1;
 if(!srw_parse(selected_file,&raw,&why)||raw.width!=6496||raw.height!=4336||raw.bits!=14||raw.compression!=32773)goto done;
 if(!p41_srw_info_ex(selected_file,selected_iq,selected_c0))goto done;
 uint64_t begin=selected_mono();
 const char*ticket=getenv("NX51_SOURCE_TICKET"),*operation=getenv("NX51_SESSION_TOKEN"),*strict=getenv("NX51_SOURCE_REHASH");
 if(!selected_query_mode&&expected_hash&&(!strict||strcmp(strict,"1")))selected_source_ticket_used=stkt_load(ticket,operation,path,expected_hash,&selected_stat,selected_sha);
 if((!selected_source_ticket_used&&!selected_hash(selected_file,selected_sha))||!selected_unchanged(selected_file))goto done;selected_hash_ns=selected_mono()-begin;
 if(expected_hash&&*expected_hash&&strcmp(expected_hash,selected_sha)){selected_settings_error="SOURCE_HASH_MISMATCH";goto done;}
 if(selected_query_mode){ok=1;goto done;}
 begin=selected_mono();struct nj_reader nr={selected_file,(uint64_t)selected_stat.st_size,selected_header_read,0};
 /* Record curves even when this source's JPEG/LDC settings are unsupported. */
 (void)stc_from_srw(&nr,&selected_tone_curve);
 if(!selected_unchanged(selected_file)){selected_settings_error="SOURCE_CHANGED_DURING_TONE_READ";goto done;}selected_tone_identity=1;nr.read_bytes=0;
 if(!nj_from_srw(&nr,&selected_jpeg_config,&selected_settings_error))goto done;
 nr.read_bytes=0;if(!lp_from_srw(&nr,&selected_lens_config,&selected_settings_error))goto done;
 /* Evidence only. Missing or unsupported tone metadata does not change the
  * existing render path and must not submit a guessed Gamma configuration. */
 nj_seal(&selected_jpeg_config,selected_sha);lp_seal(&selected_lens_config,selected_sha);
 if(!nj_valid(&selected_jpeg_config,selected_sha)||!lp_valid(&selected_lens_config,selected_sha))goto done;
 selected_settings_ns=selected_mono()-begin;selected_settings_error="SOURCE_NATIVE_JPEG_PAIR_OR_IDENTITY_INVALID";
 struct p41_reader date_reader={selected_file,(uint64_t)selected_stat.st_size,0,0};if(!selected_date(&date_reader,0,date_reader.size,selected_date_original))goto done;
 begin=selected_mono();
 jfd=openat(folder,jpg,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
 if(jfd<0){memcpy(jpg+n-4,".jpg",5);jfd=openat(folder,jpg,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);}if(jfd<0)goto done;
 if(fstat(jfd,&jpg_before)||!S_ISREG(jpg_before.st_mode)||jpg_before.st_size<1024||jpg_before.st_size>67108864)goto done;
 jf=fdopen(jfd,"rb");if(!jf)goto done;jfd=-1;
 unsigned char soi[2];if(fread(soi,1,2,jf)!=2||soi[0]!=255||soi[1]!=216)goto done;
 selected_jpeg_sha[0]=0;const char*audit=getenv("NX51_FULL_IMAGE_AUDIT");
 if(audit&&!strcmp(audit,"1")){if(!selected_hash(jf,selected_jpeg_sha))goto done;selected_native_jpeg_hashed=1;}
 if(fstat(fileno(jf),&jpg_after)||!selected_same(&jpg_before,&jpg_after))goto done;
 char jpeg_date[20];if(!selected_jpeg_date(jf,(uint64_t)jpg_before.st_size,jpeg_date)||memcmp(jpeg_date,selected_date_original,20)||fstat(fileno(jf),&jpg_after)||!selected_same(&jpg_before,&jpg_after)||!selected_unchanged(selected_file))goto done;
 strcpy(selected_path,path);snprintf(selected_jpeg,sizeof selected_jpeg,"%s%s/%s",prefix,dir,jpg);selected_jpeg_bytes=(uint64_t)jpg_before.st_size;
 selected_pair_ns=selected_mono()-begin;
 p48_selected_source=p48_sources[0];p48_selected_source.id="SELECTED_DCIM";p48_selected_source.asset_file=selected_path;
 p48_selected_source.file_bytes=(uint64_t)selected_stat.st_size;p48_selected_source.source_sha256=selected_sha;
 p48_selected_source.strip_offset=raw.offset;p48_selected_source.strip_bytes=raw.bytes;p48_selected_source.iso=selected_iq[4];
 memcpy(p48_selected_source.wb_q8,selected_iq+8,16);memcpy(p48_selected_source.black_rggb,selected_iq+16,16);
 for(unsigned i=0;i<9;i++){
  p48_selected_source.primary_s16[i]=(int16_t)selected_iq[20+i];
  for(unsigned k=0;k<2;k++){p48_selected_source.cc_s16[k][i]=(int16_t)selected_iq[29+9*k+i];p48_selected_source.c0_cc_packed[k][i]=(int16_t)p41_u16(selected_c0+524+18*k+2*i,1);}
 }
 p48_selected_source.c0=selected_c0;p48_selected_ready=1;ok=1;
done:
 if(jf)fclose(jf);if(jfd>=0)close(jfd);if(fd>=0)close(fd);if(folder>=0)close(folder);if(root>=0)close(root);
 if(!ok)selected_close();return ok;
}
static int selected_tone_report(const char*dir){
 if(!selected_tone_identity)return 1;char path[1200];
 snprintf(path,sizeof path,"%s/source-tone-curves.json",dir);FILE*tf=fopen(path,"wx");if(!tf)return 0;
 int tone_written=stc_report(tf,&selected_tone_curve,selected_sha)&&!fflush(tf)&&!fsync(fileno(tf));if(fclose(tf))tone_written=0;return tone_written;
}
static int selected_report(const char *dir){
 if(!p48_selected_ready)return 1;char path[1200];if(!selected_tone_report(dir))return 0;
 snprintf(path,sizeof path,"%s/native-jpeg-config.bin",dir);FILE*f=fopen(path,"wx");if(!f)return 0;int written=fwrite(&selected_jpeg_config,1,sizeof selected_jpeg_config,f)==sizeof selected_jpeg_config&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))written=0;if(!written)return 0;
 snprintf(path,sizeof path,"%s/native-ldc-config.bin",dir);f=fopen(path,"wx");if(!f)return 0;written=fwrite(&selected_lens_config,1,sizeof selected_lens_config,f)==sizeof selected_lens_config&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))written=0;if(!written)return 0;
 snprintf(path,sizeof path,"%s/source-settings.json",dir);f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"schema\":1,\"source_sha256\":\"%s\",\"full_jpeg_offset\":%u,\"full_jpeg_bytes\":%u,\"sampling\":422,\"quantizer_order\":\"natural\",\"ldc_profile_index\":%u,\"ldc_profile_key\":\"%s\",\"ldc_table_sha256\":\"%s\",\"ldc_generation\":\"%s\",\"ldc_approximate\":%s,\"ldc_anchor_lo\":%u,\"ldc_anchor_hi\":%u,\"ldc_fraction_q8\":%u,\"source_hash_ms\":%llu,\"settings_parse_ms\":%llu,\"native_pair_verify_ms\":%llu,\"source_ticket_used\":%s,\"native_reference_hash_checked\":%s}\n",selected_sha,selected_jpeg_config.jpeg_offset,selected_jpeg_config.jpeg_bytes,selected_lens_config.profile_index,selected_lens_config.profile_key,selected_lens_config.table_sha256,lp_method(&selected_lens_config),lp_approximate(&selected_lens_config)?"true":"false",selected_lens_config.reserved[3],selected_lens_config.reserved[4],selected_lens_config.reserved[5],(unsigned long long)(selected_hash_ns/1000000u),(unsigned long long)(selected_settings_ns/1000000u),(unsigned long long)(selected_pair_ns/1000000u),selected_source_ticket_used?"true":"false",selected_native_jpeg_hashed?"true":"false");written=!fflush(f)&&!fsync(fileno(f));if(fclose(f))written=0;if(!written)return 0;
 snprintf(path,sizeof path,"%s/selected-source.json",dir);f=fopen(path,"wx");if(!f)return 0;
 fprintf(f,"{\"date_original\":\"%s\",",selected_date_original);
 fprintf(f,"\"schema\":1,\"source_path\":\"%s\",\"source_sha256\":\"%s\",\"source_bytes\":%llu,\"source_inode\":%llu,\"source_mtime\":%lld,\"native_jpeg_path\":\"%s\",\"native_jpeg_sha256\":\"%s\",\"native_jpeg_bytes\":%llu,\"iso\":%u,\"wb_q8\":[%u,%u,%u,%u],\"black_rggb\":[%u,%u,%u,%u],\"black_profile\":%u,\"native_reference_hash_checked\":%s,\"source_cc_requested\":true,\"source_cc_applied_at_selection\":false,\"ldc_requested\":true,\"ldc_applied_at_selection\":false,\"native_snapshot_association\":\"requires_recorder_timeline\"}\n",selected_path,selected_sha,(unsigned long long)selected_stat.st_size,(unsigned long long)selected_stat.st_ino,(long long)selected_stat.st_mtime,selected_jpeg,selected_jpeg_sha,(unsigned long long)selected_jpeg_bytes,selected_iq[4],selected_iq[8],selected_iq[9],selected_iq[10],selected_iq[11],selected_iq[16],selected_iq[17],selected_iq[18],selected_iq[19],selected_iq[62],selected_native_jpeg_hashed?"true":"false");
 int ok=!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok)return 0;
 snprintf(path,sizeof path,"%s/selected-c0.bin",dir);f=fopen(path,"wx");if(!f)return 0;ok=fwrite(selected_c0,1,sizeof selected_c0,f)==sizeof selected_c0&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;if(!ok)return 0;
 const char *report=getenv("NX51_NATIVE_REPORT"),*prefix="/mnt/mmc/NXDEMO51/INIT_LENS_";int safe=report&&!strncmp(report,prefix,strlen(prefix))&&strlen(report)<160;
 if(safe)for(const char *p=report+strlen(prefix);*p;p++)if(!((*p>='0'&&*p<='9')||*p=='_'))safe=0;
 snprintf(path,sizeof path,"%s/native-recorder-link.json",dir);f=fopen(path,"wx");if(!f)return 0;
 struct timespec mono;if(clock_gettime(CLOCK_MONOTONIC,&mono)){fclose(f);return 0;}
 fprintf(f,"{\"selection_mono_ns\":%llu,",(unsigned long long)mono.tv_sec*1000000000ULL+(unsigned long long)mono.tv_nsec);
 fprintf(f,"\"schema\":1,\"recorder_report\":\"%s\",\"source_path\":\"%s\",\"source_sha256\":\"%s\",\"source_device\":%llu,\"source_inode\":%llu,\"source_bytes\":%llu,\"source_mtime\":%lld,\"source_mtime_ns\":%ld,\"native_pairing_verified\":false,\"association_rule\":\"fresh_inventory_identity_and_unambiguous_native_activity_before_module_begin\",\"retained_idle_shadow_is_not_capture_proof\":true}\n",safe?report:"",selected_path,selected_sha,(unsigned long long)selected_stat.st_dev,(unsigned long long)selected_stat.st_ino,(unsigned long long)selected_stat.st_size,(long long)selected_stat.st_mtime,selected_stat.st_mtim.tv_nsec);
 ok=!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
static int selected_rejection_report(const char*dir){
 if(!selected_tone_report(dir))return 0;
 char path[1200];snprintf(path,sizeof path,"%s/source-settings-rejected.json",dir);FILE*f=fopen(path,"wx");if(!f)return 0;
 /* Every possible reason is a constant from the bounded parsers. Source
  * pixels are never read from the failed/unknown LDC output. */
 fprintf(f,"{\"schema\":1,\"version\":\"R8m\",\"reason\":\"%s\",\"source_sha256\":\"%s\",\"native_submissions\":0,\"allocation_requests\":0,\"ldc_applied\":false}\n",selected_settings_error,selected_sha);
 int ok=!ferror(f)&&!fflush(f)&&!fsync(fileno(f));if(fclose(f))ok=0;return ok;
}
