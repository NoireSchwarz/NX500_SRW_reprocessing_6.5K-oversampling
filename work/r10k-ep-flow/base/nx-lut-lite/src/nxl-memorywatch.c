/* SPDX-License-Identifier: AGPL-3.0-only
 * Passive observation of native playback memory requests. No native API calls,
 * ptrace, IPCC access, process writes, pixel reads or LUT writes.
 */
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include "memory_core.h"
#include "memory_guards.h"
#define MAX_RECORDS 1300
struct page {uint32_t address;volatile unsigned char *data;};
static struct page pages[32];static unsigned page_count;
static uint32_t last_valid_va;
static uint32_t records[MAX_RECORDS][MEM_WORDS];
static volatile sig_atomic_t stopped;
static void stop(int sig){(void)sig;stopped=1;}
static uint64_t now_ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000u+(unsigned)t.tv_nsec;}
static int text_file(const char *path,char *out,size_t size){FILE *f=fopen(path,"r");if(!f)return 0;int ok=fgets(out,(int)size,f)!=NULL;fclose(f);if(ok)out[strcspn(out,"\r\n")]=0;return ok;}
static int supported(void){char v[64],m[64];FILE *f=fopen("/etc/version.info","r");if(!f)return 0;int ok=fgets(v,sizeof v,f)&&fgets(m,sizeof m,f);fclose(f);if(!ok)return 0;v[strcspn(v,"\r\n")]=0;m[strcspn(m,"\r\n")]=0;return !strcmp(v,"1.12")&&!strcmp(m,"NX500");}
static int camera_pid(void){DIR *d=opendir("/proc");if(!d)return -1;struct dirent *e;int found=0;while((e=readdir(d))){if(!e->d_name[0]||strspn(e->d_name,"0123456789")!=strlen(e->d_name))continue;char p[320],name[64];snprintf(p,sizeof p,"/proc/%s/comm",e->d_name);if(text_file(p,name,sizeof name)&&!strcmp(name,"di-camera-app")){if(found){found=-1;break;}found=atoi(e->d_name);}}closedir(d);return found>1?found:-1;}
static int original_app(void){FILE *f=fopen("/usr/apps/com.samsung.di-camera-app/bin/di-camera-app","rb");if(!f)return 0;unsigned char b[4096];size_t n,total=0;uint32_t h=2166136261u;while((n=fread(b,1,sizeof b,f))){total+=n;for(size_t i=0;i<n;i++)h=(h^b[i])*16777619u;}int ok=!ferror(f)&&total==MEM_APP_BYTES&&h==MEM_APP_FNV;fclose(f);return ok;}
static int ram_copy(int fd,uint32_t address,void *out,size_t size){
 if(!mem_metadata_range(address,size))return 0;
 size_t done=0;while(done<size){uint32_t p=(address+(uint32_t)done)&~4095u;struct page *map=NULL;
  for(unsigned i=0;i<page_count;i++)if(pages[i].address==p){map=pages+i;break;}
  if(!map){if(page_count==32)return 0;map=pages+page_count++;map->address=p;map->data=mmap(NULL,4096,PROT_READ,MAP_SHARED,fd,(off_t)p);if(map->data==MAP_FAILED){map->data=NULL;return 0;}}
  if(!map->data)return 0;size_t off=address+done-p,take=4096-off;if(take>size-done)take=size-done;
  for(size_t i=0;i<take;i++)((unsigned char*)out)[done+i]=map->data[off+i];done+=take;
 }return 1;
}
static int copy_file(const char *source,const char *target){FILE *a=fopen(source,"rb"),*b=NULL;if(!a)return 0;b=fopen(target,"wb");if(!b){fclose(a);return 0;}char buf[4096];size_t n;int bad=0;while((n=fread(buf,1,sizeof buf,a)))if(fwrite(buf,1,n,b)!=n){bad=1;break;}if(ferror(a))bad=1;fclose(a);if(fclose(b))bad=1;return !bad;}
static int mappings(int pid,const char *directory,const char *phase,struct mem_mapping *maps,unsigned *count){
 char path[128],target[1200],line[512];snprintf(path,sizeof path,"/proc/%d/maps",pid);snprintf(target,sizeof target,"%s/camera_maps_%s.txt",directory,phase);
 if(!copy_file(path,target))return 0;FILE *f=fopen(target,"r");if(!f)return 0;*count=0;
 while(fgets(line,sizeof line,f)){struct mem_mapping m;if(mem_mapping_line(line,&m)&&*count<64)maps[(*count)++]=m;}fclose(f);return *count>0;
}
static int runtime_guards(int proc){uint32_t words[8];for(unsigned i=0;i<MEM_GUARD_COUNT;i++){if(pread(proc,words,sizeof words,mem_guards[i].address)!=(ssize_t)sizeof words||memcmp(words,mem_guards[i].words,sizeof words))return 0;}return 1;}
static int choose_pa(const struct mem_mapping *maps,unsigned count,uint32_t va,uint32_t *pa,struct mem_mapping *chosen){int seen=0;for(unsigned i=0;i<count;i++){uint32_t candidate;if(mem_translate(maps+i,va,PB_REQUEST_BYTES,&candidate)){if(seen&&candidate!=*pa)return 0;*pa=candidate;*chosen=maps[i];seen=1;}}return seen;}
static void sample(int proc,int dev,const struct mem_mapping *maps,unsigned map_count,uint32_t *r,unsigned seq,unsigned elapsed,int pid){
 uint64_t begin=now_ns();uint32_t factory[12],head[66],counter[10],later[10],root_after[12],a[4],b[4];memset(r,0,128);r[0]=0x314d454du;r[1]=seq;r[2]=elapsed;r[3]=2;r[25]=PB_REQUEST_BYTES;r[26]=(uint32_t)pid;
 if(pread(proc,r+4,4,PB_POINTER_VA)!=4)goto done;
 r[3]=3;if(!ram_copy(dev,0x817dc498u,factory,sizeof factory)||factory[0]!=0x80574230u||factory[1]!=0x805742acu||factory[9]!=MEM_MANAGER)goto done;
 r[7]=factory[9];if(!ram_copy(dev,MEM_MANAGER,head,sizeof head)||head[0]!=0x80574b60u||head[65]!=0x80574c48u||!ram_copy(dev,MEM_MANAGER+0xb0108u,counter,sizeof counter))goto done;
 r[8]=counter[0];r[9]=counter[1];r[10]=counter[2];if(r[8]!=0x94000000u||r[9]!=45056u||r[10]>r[9])goto done;
 if(!ram_copy(dev,MEM_MANAGER+0xb05a0u,r+23,4))goto done;
 r[24]=r[4]?r[4]:last_valid_va;r[21]=!r[4]&&last_valid_va?1:0;
 if(r[24]){struct mem_mapping mapping={0};uint32_t entry;r[3]=5;if(!choose_pa(maps,map_count,r[24],r+6,&mapping)||!mem_entry_address(r[6],r[8],r[23],r+12,&entry))goto done;
  if(r[4])last_valid_va=r[4];
  r[18]=mapping.lo;r[19]=mapping.hi;r[20]=mapping.physical;
  if(!ram_copy(dev,entry,a,16)||!ram_copy(dev,entry,b,16))goto done;memcpy(r+13,a,16);if(memcmp(a,b,16)){r[3]=6;goto done;}r[22]=(a[0]&255u)!=0;
 }
 r[3]=2;if(pread(proc,r+5,4,PB_POINTER_VA)!=4)goto done;
 if(!ram_copy(dev,MEM_MANAGER+0xb0108u,later,sizeof later)||!ram_copy(dev,0x817dc498u,root_after,sizeof root_after)){r[3]=3;goto done;}
 r[11]=later[2];r[3]=memcmp(factory,root_after,sizeof factory)?4:r[4]!=r[5]?1:memcmp(counter,later,sizeof counter)?6:0;
done:r[17]=(uint32_t)((now_ns()-begin)/1000);if(r[17]>10000)r[3]=7;r[31]=mem_crc(r,124);
}
int main(int argc,char **argv){
 if(argc==2&&!strcmp(argv[1],"--version")){puts("NXL_MEMORYWATCH=0.3.3-native-memory; READ_ONLY; NO_NATIVE_CALLS");return 0;}
 if(argc==2&&!strcmp(argv[1],"--clock")){struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);clock_gettime(CLOCK_REALTIME,&b);printf("mono=%lld.%09ld realtime=%lld.%09ld\n",(long long)a.tv_sec,a.tv_nsec,(long long)b.tv_sec,b.tv_nsec);return 0;}
 if(argc!=2||argv[1][0]!='/'||strlen(argv[1])>1000)return 1;
 const char *dir=argv[1];char path[1200];int rc=1,proc=-1,dev=-1;unsigned count=0,map_count=0,accepted=0;struct mem_mapping maps[64];FILE *csv=NULL;
 snprintf(path,sizeof path,"%s/probe.log",dir);FILE *log=fopen(path,"w");if(!log)return 1;
#define FAIL(code,message) do{rc=(code);fprintf(log,"ERROR %s errno=%d\n",(message),errno);goto finish;}while(0)
 if(!supported())FAIL(10,"NX500_1.12_REQUIRED");if(!original_app())FAIL(11,"APP_BINARY_MISMATCH");
 int pid=camera_pid();if(pid<0)FAIL(12,"CAMERA_PROCESS_NOT_UNIQUE");fprintf(log,"CAMERA_PID=%d PB_POINTER_VA=0x%08x REQUEST_BYTES=%u\n",pid,PB_POINTER_VA,PB_REQUEST_BYTES);
 snprintf(path,sizeof path,"/proc/%d/mem",pid);proc=open(path,O_RDONLY|O_CLOEXEC);if(proc<0)FAIL(13,"PROC_MEM_READ_UNAVAILABLE_NO_PTRACE_ATTEMPTED");
 if(!runtime_guards(proc))FAIL(14,"APP_RUNTIME_CODE_GUARD_MISMATCH");if(!mappings(pid,dir,"begin",maps,&map_count))FAIL(15,"POOL_MAPPING_UNAVAILABLE");
 dev=open("/dev/mem",O_RDONLY|O_SYNC|O_CLOEXEC);if(dev<0)FAIL(16,"READ_ONLY_PHYSICAL_METADATA_UNAVAILABLE");
 for(int tries=0;tries<10;tries++){sample(proc,dev,maps,map_count,records[0],0,0,pid);if(!records[0][3])break;struct timespec pause={0,50000000};nanosleep(&pause,NULL);}
 if(records[0][3]!=0)FAIL(17,"METADATA_PREFLIGHT_FAILED");
 snprintf(path,sizeof path,"%s/samples.csv",dir);csv=fopen(path,"w");if(!csv)FAIL(18,"CSV_CREATE_FAILED");
 fputs("sequence,elapsed_ms,status,ui_va_before,ui_va_after,tracked_va,tracking_previous_pointer,physical,manager,pool_base,total_blocks,free_before,free_after,block_index,entry_word0,span_blocks,entry_word2,entry_word3,read_us,crc32\n",csv);
 signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGALRM,stop);alarm(75);
 uint64_t started=now_ns();snprintf(path,sizeof path,"%s/READY",dir);FILE *ready=fopen(path,"w");if(!ready)FAIL(18,"READY_CREATE_FAILED");fprintf(ready,"READ_ONLY_NATIVE_PLAYBACK_TRACE pid=%d duration=60s\n",pid);fclose(ready);
 fprintf(log,"READY runtime_guards=%u map_count=%u pixel_reads=0 native_calls=0 process_writes=0 ptrace=0\n",(unsigned)MEM_GUARD_COUNT,map_count);fflush(log);
 while(!stopped&&count<MAX_RECORDS){uint64_t now=now_ns();unsigned elapsed=(unsigned)((now-started)/1000000);if(elapsed>=60000)break;uint32_t *r=records[count];sample(proc,dev,maps,map_count,r,count,elapsed,pid);if(!r[3])accepted++;
  fprintf(csv,"%u,%u,%u,0x%08x,0x%08x,0x%08x,%u,0x%08x,0x%08x,0x%08x,%u,%u,%u,%u,0x%08x,%u,0x%08x,0x%08x,%u,%08x\n",r[1],r[2],r[3],r[4],r[5],r[24],r[21],r[6],r[7],r[8],r[9],r[10],r[11],r[12],r[13],r[14],r[15],r[16],r[17],r[31]);count++;if(!(count%20))fflush(csv);
  if(r[3]==2){stopped=1;break;}struct timespec delay={0,50000000};while(nanosleep(&delay,&delay)&&errno==EINTR&&!stopped){}
 }
 alarm(0);rc=stopped?20:count==MAX_RECORDS?21:!accepted?23:0;
 snprintf(path,sizeof path,"%s/samples.nxmem",dir);FILE *out=fopen(path,"wb");if(!out)FAIL(18,"BINARY_CREATE_FAILED");uint32_t h[32]={0};memcpy(h,"NXMEM1",6);h[2]=1;h[3]=128;h[4]=128;h[5]=count;h[6]=(uint32_t)pid;h[7]=60000;h[8]=50;h[9]=PB_POINTER_VA;h[10]=MEM_MANAGER;h[11]=PB_REQUEST_BYTES;h[12]=accepted;h[13]=(uint32_t)rc;h[31]=mem_crc(h,124);
 int bad=fwrite(h,1,128,out)!=128||fwrite(records,128,count,out)!=count;if(fflush(out)||fsync(fileno(out)))bad=1;if(fclose(out))bad=1;if(bad)FAIL(18,"BINARY_SAVE_FAILED");
 {struct mem_mapping end_maps[64];unsigned n;if(!mappings(pid,dir,"end",end_maps,&n))rc=22;}
 fprintf(log,"CAPTURE_END records=%u accepted=%u status=%d stopped=%d\n",count,accepted,rc,stopped);
finish:
 if(csv){int bad=fflush(csv)||fsync(fileno(csv));if(fclose(csv))bad=1;if(bad)rc=18;}
 if(proc>=0)close(proc);for(unsigned i=0;i<page_count;i++)if(pages[i].data)munmap((void*)pages[i].data,4096);if(dev>=0)close(dev);
 fprintf(log,"RESULT exit=%d samples=%u accepted=%u\n",rc,count,accepted);fclose(log);return rc;
}
