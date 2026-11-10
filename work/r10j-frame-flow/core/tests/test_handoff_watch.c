#undef NDEBUG
#define main passive_observer_entry_not_run
#include "nxl-handoff-watch.c"
#undef main
#include <assert.h>
#include "session_rearm_contract.h"
int main(void){
 assert(!r7_native_worker_name("nxl-handoff-wat"));assert(r7_native_worker_name("nxl-demo-front"));
 assert(observer_path("/mnt/mmc/NXDEMO51/INIT_20261106_100000_900"));
 assert(observer_path("/mnt/mmc/NXDEMO51/JOB_123/handoff-watch-front-1"));
 const char *bad[]={"/mnt/mmc/NXDEMO51/JO_1","/mnt/mmc/NXDEMO51/INIT_../x","/mnt/mmc/DCIM/INIT_1","/mnt/mmc/NXDEMO51/INIT_x;id","/mnt/mmc/NXDEMO51/INIT_x\n"};
 for(unsigned i=0;i<sizeof bad/sizeof *bad;i++)assert(!observer_path(bad[i]));
 assert(!setenv("NX51_SESSION_TOKEN","old-round",1));close_inherited();assert(!getenv("NX51_SESSION_TOKEN"));
 assert(process_identity(123)==22);assert(!process_identity(124));
 char map_output[16384]={0};FILE *map_stream=fmemopen(map_output,sizeof map_output,"w+");assert(map_stream);
 events=map_stream;camera=123;maps_sample(0);assert(!fclose(map_stream));assert(udd_base==0x76000000u);
 assert(strstr(map_output,"libudd5.so")&&strstr(map_output,"possibly_truncated\":false"));
 dev=99;uint32_t value=0xa55au;
 const uint32_t denied[]={0x20820000u,0x80000000u,0x94000000u,0x8153cbddu};
 for(unsigned i=0;i<sizeof denied/sizeof *denied;i++)assert(!word(denied[i],&value)&&value==0xa55au);
 assert(word(SLA_SEM_A,&value)&&value==165u);assert(word(SLA_SEM_B,&value)&&value==166u);
 assert(page_count==1);assert(!typed_reader(NULL,0x20820000u,&value,1));assert(!typed_reader(NULL,0x80b84280u,&value,1));
 assert(typed_reader(NULL,0x80592640u,&value,1)&&value==0x801daa38u);assert(page_count==2);
 assert(crc32("123456789",9)==0xcbf43926u);
 unsigned char output[1024]={0};FILE *f=fmemopen(output,sizeof output,"w+");assert(f);
 uint32_t payload[3]={7,0xffffffffu,0x700d1234u};assert(frame(f,4,31,10000000001ULL,10000000005ULL,payload,sizeof payload));
 assert(!fflush(f));off_t length=ftello(f);assert(length==52);assert(!fclose(f));
 uint32_t header[10];memcpy(header,output,40);assert(header[9]==crc32(header,36)&&header[8]==crc32(payload,sizeof payload));
 printf("{\"frame_hex\":\"");for(unsigned i=0;i<(unsigned)length;i++)printf("%02x",output[i]);puts("\"}");
 char escaped[256]={0};f=fmemopen(escaped,sizeof escaped,"w+");assert(f);quoted(f,"quote\" slash\\ newline\n tab\t");assert(!fclose(f));puts(escaped);
 dev=-1;events=stdout;ram_have=0;ram_sample(0);
 char schema_text[8192]={0};f=fmemopen(schema_text,sizeof schema_text,"w+");assert(f);schema_write(f);assert(!fclose(f));fputs(schema_text,stdout);
 puts("PASS_OBSERVER_PATH_CRC_JSON_IDENTITY_RDONLY_MAPPING_AND_DESCRIPTOR_CLEANUP");return 0;
}
