/* Actual helper readers/snapshot/report; only physical mapping is redirected. */
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <assert.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <stdio.h>
static void *test_mmap(void*,size_t,int,int,int,off_t);
static int test_munmap(void*,size_t);
#define mmap test_mmap
#define munmap test_munmap
#define main production_main
#include "../src/nxl-oem-restore.c"
#undef main
#undef mmap
#undef munmap
static unsigned checks,map_calls,map_mode,model_state=1,model_request;
static unsigned char *model_page;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void *test_mmap(void *where,size_t bytes,int prot,int flags,int fd,off_t offset){
 (void)where;assert(bytes==4096&&prot==PROT_READ&&flags==MAP_SHARED&&fd==1234&&offset==(off_t)(last_read_address&~4095u));map_calls++;
 if(map_mode==1){errno=EACCES;return MAP_FAILED;}
 assert(!mprotect(model_page,4096,PROT_READ|PROT_WRITE));memset(model_page,0,4096);uint32_t value=0;uint32_t a=last_read_address;
 if(a==OEM_PROCESSOR)value=0x81000100;
 else if(a==0x81000100)value=OEM_VTABLE;
 else if(a==0x81000100+65)value=model_state;
 else if(a==OEM_REQUEST)value=model_request;
 else if(a==FR_TASK+3428)value=0x81000200;
 else if(a==0x81000200)value=0x805926c8;
 else if(a==0x81543d40)value=0x81000300;
 else if(a==0x81000300)value=1;
 else for(unsigned i=0;i<FR_FIELDS;i++)if(a==fr_descriptors[i].address&&fr_descriptors[i].rule==FR_EQUAL){value=fr_descriptors[i].expected;break;}
 memcpy(model_page+(a&4095u),&value,last_read_bytes<4?last_read_bytes:4);assert(!mprotect(model_page,4096,map_mode==2?PROT_NONE:PROT_READ));return model_page;
}
static int test_munmap(void *p,size_t bytes){assert(p==model_page&&bytes==4096);return 0;}
static void app(uint32_t ui,uint32_t expected){uint32_t handle=0x12345678;assert(pwrite(procfd,&ui,4,OEM_UI_RUNNING)==4&&pwrite(procfd,&expected,4,OEM_EXPECTED)==4&&pwrite(procfd,&handle,4,OEM_HANDLE)==4);}
int main(int argc,char **argv){
 assert(argc==2);directory=argv[1];assert(!mkdir(directory,0700));report_fd=open(directory,O_RDONLY|O_DIRECTORY);assert(report_fd>=0);char p[1200];snprintf(p,sizeof p,"%s/restore.log",directory);log_file=fopen(p,"w+");snprintf(p,sizeof p,"%s/trace_events.txt",directory);trace_file=fopen(p,"w+");snprintf(p,sizeof p,"%s/snapshots.jsonl",directory);snapshots=fopen(p,"w+");assert(log_file&&trace_file&&snapshots);started=ns();stage("TEST_INITIAL");
 model_page=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);assert(model_page!=MAP_FAILED);devfd=1234;uint32_t word=0;
 CHECK(!mmio_ro32(0x20810309,&word)&&!map_calls);CHECK(!mmio_ro32(0x2081030c,&word)&&!map_calls);CHECK(!mmio_ro32(0x8142a6f0,&word)&&!map_calls);CHECK(!ram_ro_bytes(0x20810308,&word,4)&&!map_calls);CHECK(!ram_ro_bytes(0x93ffffff,&word,4)&&!map_calls);CHECK(!mmio_ro_words(P41_LCAC,&word,7)&&!map_calls);CHECK(!mmio_ro_words(P41_LCAC+4,&word,6)&&!map_calls);
 map_mode=1;CHECK(!mmio_ro32(0x20810308,&word)&&map_calls==1&&!read_faults&&last_read_width==4);read_failures=0;map_mode=0;CHECK(mmio_ro32(0x20810308,&word)&&last_read_width==4&&!word);
 stage("TEST_FAULT_PRE_SUBMIT");map_mode=2;CHECK(!mmio_ro32(0x20810308,&word)&&read_faults==1&&last_fault_signal==SIGSEGV&&last_fault_address==0x20810308&&last_read_width==4);CHECK(!submitted&&!api_inflight&&!original_command_pending&&!native_calls&&!strcmp(classification,"SAFE_NO_SUBMIT"));CHECK(finish_report(1));
 rewind(log_file);char logs[10000]={0};assert(fread(logs,1,sizeof logs-1,log_file)>0);CHECK(strstr(logs,"READ_BEGIN stage=TEST_FAULT_PRE_SUBMIT")&&strstr(logs,"address=0x20810308 width=4")&&strstr(logs,"READ_FAULT signal="));
 map_mode=0;read_failures=read_faults=0;snprintf(p,sizeof p,"%s/process-memory.bin",directory);procfd=open(p,O_RDWR|O_CREAT|O_EXCL,0600);assert(procfd>=0&&!ftruncate(procfd,0x500000));region_count=1;regions[0].lo=0x1000;regions[0].hi=0x500000;strcpy(regions[0].perms,"rw-p");struct oem_snapshot s;
 app(1,0);model_state=2;model_request=1;map_calls=0;CHECK(snapshot_state(&s)&&!oem_preflight(&s)&&map_calls==4);
 app(0,0);model_state=1;model_request=0;map_calls=0;CHECK(snapshot_state(&s)&&!oem_preflight(&s)&&map_calls==4);
 app(1,1);map_calls=0;CHECK(snapshot_state(&s)&&!oem_preflight(&s)&&map_calls==4);
 app(1,0);model_request=1;map_calls=0;CHECK(snapshot_state(&s)&&!oem_preflight(&s)&&map_calls==4);
 model_request=0;map_calls=0;CHECK(snapshot_state(&s)&&oem_preflight(&s)&&map_calls>24);
 submitted=1;model_state=2;model_request=1;map_calls=0;CHECK(snapshot_state(&s)&&oem_live(&s)&&map_calls>24);
 CHECK(!native_calls&&!api_inflight);fclose(snapshots);fclose(trace_file);fclose(log_file);close(procfd);close(report_fd);munmap(model_page,4096);
 printf("PASS %u actual-reader/snapshot/report integration checks; physical mapping modeled; zero native calls\n",checks);return 0;
}
