"""Validate original-allocation absence and consume real V2 producer receipts.
ARM execution only; private read/stat/env services model the receipt files.
"""
from pathlib import Path
import hashlib,json,subprocess,sys
R=Path(__file__).resolve().parents[1];W=R.parent;V=R/'validation/r7k'
sys.path[:0]=[str(W/'tooling/python'),str(R/'tools')]
from arm_model import ArmModel
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def build(source,target):
 p=subprocess.run([sys.executable,str(R/'core/tools/build_front.py'),'--zig',str(W/'tooling/python/ziglang/zig.exe'),'--cross-runtime',str(W/'tooling/arm-linux-musleabi-cross'),
  '--source',str(source),'--output',str(target)],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=120)
 assert p.returncode==0,(p.stdout,p.stderr)
def arr(name,data):return 'static const unsigned char '+name+'[]={'+','.join(str(x) for x in data)+'};\n'
fixture=R/'core/tests/fixtures/r7k-lifetime';records=[]
text='#include <assert.h>\n#include <stdio.h>\n#include "'+(R/'core/src/allocation_lifetime.h').as_posix()+'"\n'
text+=arr('real_retained',(fixture/'retained-front.bin').read_bytes())+arr('real_reused',(fixture/'reused-allocator-0.bin').read_bytes())
text+=r'''
static uint32_t table[R7_BLOCKS*4u];static struct r7_allocation_lifetime m;static unsigned checks;
#define CHECK(x) do{assert(x);checks++;}while(0)
static void extent(unsigned start,unsigned blocks,unsigned auxiliary){for(unsigned j=0;j<blocks;j++){table[(start+j)*4]=35;table[(start+j)*4+1]=blocks-j;table[(start+j)*4+2]=j?0:auxiliary;}}
int main(void){uint32_t f=0xa1c00000u,b=0xaf65c000u;unsigned start=(f-0x94000000u)>>14;
memcpy(table,real_retained,sizeof table);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_SAME_SHAPE);CHECK(!r7_allocation_absent(m.front_state));CHECK(!r7_reused_reference(&m,f));
memcpy(table,real_reused,sizeof table);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_REUSED);CHECK(m.backend_state==R7_RANGE_VACANT);CHECK(m.head_count==8);CHECK(m.free_blocks==26396);CHECK(r7_reused_reference(&m,f));CHECK(!r7_reused_reference(&m,b));
memset(table,0,sizeof table);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_VACANT&&m.backend_state==R7_RANGE_VACANT);CHECK(m.free_blocks==R7_BLOCKS);CHECK(!r7_reused_reference(&m,f));
extent(start,8192,123);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_SAME_SHAPE);table[start*4+2]=999;CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_SAME_SHAPE); /* no invented generation ID */
for(unsigned split=1;split<8192;split+=257){memset(table,0,sizeof table);extent(start,split,1);extent(start+split,8192-split,2);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_REUSED);CHECK(r7_reused_reference(&m,f));CHECK(r7_reused_reference(&m,f+134217728u-1));}
memset(table,0,sizeof table);extent(start-64,8192+128,3);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_REUSED);CHECK(r7_reused_reference(&m,f));
/* Unmarked interior slots remain covered by the real head. */
memset(table,0,sizeof table);extent(start,8192,1);memset(table+(start+1)*4,0,8191u*16u);CHECK(r7_allocation_layout(table,f,b,&m));CHECK(m.front_state==R7_RANGE_SAME_SHAPE);CHECK(!r7_reused_reference(&m,f+16384));
/* Inconsistent extents/interiors never authorize reuse. */
for(unsigned test=0;test<6;test++){memset(table,0,sizeof table);extent(start,640,1);switch(test){case 0:table[start*4+1]=0;break;case 1:table[start*4+1]=45056;break;case 2:table[(start+1)*4+1]=1;break;case 3:table[(start+1)*4]=36;break;case 4:table[(start+1)*4]=0;break;case 5:table[(start-1)*4+1]=1;break;}CHECK(!r7_allocation_layout(table,f,b,&m));CHECK(!m.valid);CHECK(!r7_reused_reference(&m,f));}
printf("PASS allocation lifetime: actual retained/reused tables, same-shape ABA held, 32 different partitions, parent/new extents, unmarked interiors, malformed layout; checks=%u\n",checks);return 0;}
'''
source=V/'lifetime-contract.c';source.write_text(text,encoding='utf8');target=V/'lifetime-contract.arm';build(source,target)
result=ArmModel(target).run();assert result['exit']==0,result;records.append(dict(name='allocation-lifetime',**result));print(result['stdout'].strip(),flush=True)

# Feed actual successful producer receipts through the actual session parser.
free=(V/'field-free-1-REARM.proof').read_bytes();reused=(V/'reused-field-3-REARM.proof').read_bytes()
assert free.startswith(b'NX51REARMPROOF2\n') and b'front_range_state=REUSED\n' in reused
source=V/'receipt-v2.c'
text='''#define open model_open
#define read model_read
#define close model_close
#define fstat model_fstat
#define lstat model_lstat
#define getenv model_getenv
#define main production_session_main_not_run
#include "'''+(R/'session/src/session.c').as_posix()+'''"
#undef main
#undef open
#undef read
#undef close
#undef fstat
#undef lstat
#undef getenv
#include <assert.h>
'''+arr('free_receipt',free+b'\0')+arr('reused_receipt',reused+b'\0')+r'''
static char active[4096];static size_t offset;static unsigned bad_type,short_read,checks;
static const char token_value[]="0123456789abcdef0123456789abcdef";
char *model_getenv(const char *key){assert(!strcmp(key,"NX51_SESSION_TOKEN"));return (char*)token_value;}
int model_open(const char *path,int flags,...){assert(strstr(path,"/REARM.proof")&&!(flags&O_ACCMODE)&&(flags&O_NOFOLLOW));offset=0;return 42;}
ssize_t model_read(int fd,void *out,size_t n){assert(fd==42);if(short_read)return 0;size_t z=strlen(active)-offset;if(n>z)n=z;memcpy(out,active+offset,n);offset+=n;return (ssize_t)n;}
int model_close(int fd){assert(fd==42);return 0;}
int model_fstat(int fd,struct stat *st){assert(fd==42);memset(st,0,sizeof *st);st->st_mode=bad_type?S_IFDIR:S_IFREG;st->st_size=strlen(active);return 0;}
int model_lstat(const char *path,struct stat *st){(void)path;memset(st,0,sizeof *st);st->st_mode=S_IFDIR;return 0;}
static void replace(const char *before,const char *after){char copy[4096];char *at=strstr(active,before);assert(at);size_t n=(size_t)(at-active);memcpy(copy,active,n);int z=snprintf(copy+n,sizeof copy-n,"%s%s",after,at+strlen(before));assert(z>0&&(size_t)z<sizeof copy-n);strcpy(active,copy);}
static void seal(void){char *sum=strstr(active,"sha256=");assert(sum);size_t n=(size_t)(sum-active);char hash[65];digest(active,n,hash);snprintf(active+n,sizeof active-n,"sha256=%s\nend=NX51REARMEND2\n",hash);}
#define CHECK(x) do{assert(x);checks++;}while(0)
int main(void){struct session s={0},previous={0};strcpy(s.job,"/mnt/mmc/NXDEMO51/JOB_TEST");strcpy(previous.job,"/mnt/mmc/NXDEMO51/JOB_OLD");const char *dir="/mnt/mmc/NXDEMO51/JOB_TEST/rearm_0123456789abcdef0123456789abcdef";
strcpy(active,(const char*)free_receipt);CHECK(rearm_receipt(&s,&previous,dir));strcpy(active,(const char*)reused_receipt);CHECK(rearm_receipt(&s,&previous,dir));
const char *keys[]={"front_allocation_retired=1","backend_allocation_retired=1","allocator_layout_verified=1","allocator_final_revalidated=1","backend_resources_retired=1","native_outcome_resolved=1","known_roots_clear=1","code_original=1","shell_parked=1","queue_empty=1","workers_idle=1","readonly=1"};
for(unsigned i=0;i<sizeof keys/sizeof *keys;i++){strcpy(active,(const char*)reused_receipt);char *p=strstr(active,keys[i]);assert(p);p[strlen(keys[i])-1]='0';seal();CHECK(!rearm_receipt(&s,&previous,dir));}
strcpy(active,(const char*)reused_receipt);replace("front_range_state=REUSED","front_range_state=SAME_SHAPE_UNPROVEN");seal();CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);replace("backend_range_state=VACANT","backend_range_state=INVALID");seal();CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);replace("token=0123456789abcdef0123456789abcdef","token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");seal();CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);replace("previous=/mnt/mmc/NXDEMO51/JOB_OLD","previous=/mnt/mmc/NXDEMO51/JOB_OTHER");seal();CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);replace("NX51REARMPROOF2","NX51REARMPROOF1");seal();CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);char *sum=strstr(active,"sha256=");sum[7]=sum[7]=='a'?'b':'a';CHECK(!rearm_receipt(&s,&previous,dir));
strcpy(active,(const char*)reused_receipt);CHECK(!rearm_receipt(&s,&previous,"/mnt/mmc/NXDEMO51/JOB_TEST/rearm_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
bad_type=1;CHECK(!rearm_receipt(&s,&previous,dir));bad_type=0;short_read=1;CHECK(!rearm_receipt(&s,&previous,dir));
printf("PASS actual session V2 parser: actual vacant/reused producer receipts, no false-free assertion, required facts/token/predecessor/path/hash/version/read/type failures; checks=%u\n",checks);return 0;}
'''
source.write_text(text,encoding='utf8');target=V/'receipt-v2.arm';build(source,target);result=ArmModel(target).run();assert result['exit']==0,result
records.append(dict(name='actual-session-V2-parser',**result));print(result['stdout'].strip(),flush=True)
target=V/'reset-locks.arm';build(R/'session/tests/test_reset_locks_r7f.c',target);result=ArmModel(target).run();assert result['exit']==0,result
records.append(dict(name='unchanged-reset-lock-gates',**result));print(result['stdout'].strip(),flush=True)
report=dict(format='NX51_R7K_ALLOCATION_AND_RECEIPT_CONTRACT1',passed=True,hardware_tested=False,checks=records,
 source_sha256={str(p.relative_to(R)):sha(p) for p in [R/'core/src/allocation_lifetime.h',R/'session/src/rearm.inc',R/'session/src/session.c']},
 actual_producer_receipts={'vacant':sha(V/'field-free-1-REARM.proof'),'reused':sha(V/'reused-field-3-REARM.proof')},
 scope='Compiled ARM lifetime predicates and actual session receipt/lock parser. File/stat/environment services explicitly modeled; no live camera ownership or native calls executed.')
(V/'lifetime-and-receipt.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
print(json.dumps(dict(passed=True,ARM_cases=len(records))),flush=True)
