from pathlib import Path
import sys,json,re,struct,subprocess,os,hashlib,gc,time
W=Path(sys.argv[1]).resolve()if len(sys.argv)>1 else Path(__file__).resolve().parent;R=W.parent/'repository/nx500';T=W/'build-tree';B=W.parent/'r11k3-srw-restore-work/build-tree';sys.path[:0]=[str(R/'work/tooling/python'),str(T/'tools')]
from post_file_model import PostFileModel
runs=W/('ep-tests-'+str(time.time_ns()));runs.mkdir();env=dict(os.environ,PYTHONUTF8='1',PYTHONDONTWRITEBYTECODE='1',ZIG_GLOBAL_CACHE_DIR=str(W/'zig-global'),ZIG_LOCAL_CACHE_DIR=str(W/'zig-local'))
prod=(T/'hardware/src/nxl-allocprobe.c').read_text('utf8');start=prod.index('static int ep_preflight(');end=prod.index('\nstatic int ep_kernel_read',start);body=prod[start:end]
header=r'''
#ifndef NXL_REAL_EP_PREFLIGHT_FIXTURE
#define NXL_REAL_EP_PREFLIGHT_FIXTURE
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../../common/shared_workspace_io.h"
#ifndef NXL_TEST_EP_ALLOC_HEADER
#define NXL_TEST_EP_ALLOC_HEADER "../../hardware/src/alloc_core.h"
#endif
#include NXL_TEST_EP_ALLOC_HEADER
static uint32_t rpe_response[5],rpe_last,rpe_native_calls,rpe_owned_calls,rpe_reads,rpe_fault,rpe_count;
static uint32_t rpe_udd_base=0x50000000u,rpe_tint_base=0x51000000u,rpe_type_base=0x52000000u;
static unsigned rpe_ep_queries,rpe_ep_matches,rpe_ep_open_count,rpe_ep_context_observed,rpe_ep_fd_verified,rpe_ep_contract_verified,rpe_stop_requested;
static int rpe_ep_sma_fd;
static const char*rpe_ep_error="NONE",*rpe_trace_error,*rpe_trace_phase;
static FILE*rpe_log_file;
static int rpe_still_owned(void){rpe_owned_calls++;return rpe_fault!=5||rpe_owned_calls<3;}
static void rpe_event(const char*s){fprintf(rpe_log_file,"%s\n",s);}
static int rpe_read_word(uint32_t a,uint32_t*v){
 if(rpe_fault==1)return 0;
 if(a==rpe_udd_base+EP_OPEN_COUNT_DATA){rpe_reads++;*v=rpe_count+(rpe_fault==7&&rpe_reads>=3);return 1;}
 if(a==rpe_udd_base+EP_SMA_FD_DATA){*v=3;return 1;}assert(0);return 0;
}
static int rpe_invoke(uint32_t fn,uint32_t arg,unsigned timeout){
 assert(timeout==3000);rpe_native_calls++;
 if(fn==rpe_udd_base+EP_OPEN_COUNT_FN){assert(!arg);rpe_last=rpe_count+(rpe_fault==4);return 1;}
 assert(fn==rpe_udd_base+EP_SMA_VTOP_FN);assert(arg>=rpe_response[0]&&(uint64_t)arg<rpe_response[0]+(uint64_t)rpe_response[2]);
 if(rpe_fault==6)return 0;rpe_last=rpe_response[1]+arg-rpe_response[0]+(rpe_fault==3?4096u:0);return 1;
}
static int rpe_get_regs(struct alloc_regs*r){memset(r,0,sizeof *r);r->r[0]=rpe_last;return 1;}
static int rpe_sma_target(char b[256]){strcpy(b,rpe_fault==2?"/dev/null":"/dev/d5_sma");return rpe_fault!=2;}
'''
names=['ep_preflight','response','still_owned','ep_error','trace_error','trace_phase','ep_sma_fd','udd_base','tint_base','type_base','log_file','ep_open_count','ep_context_observed','ep_fd_verified','ep_contract_verified','ep_queries','ep_matches','stop_requested','read_word','invoke','get_regs','sma_target','event']
header+='\n'.join('#define '+n+' rpe_'+n for n in names)+'\n'+body+'\n'+'\n'.join('#undef '+n for n in names)+'\n'
header+=r'''
static int rpe_run(const char*dir,const struct sw_lease*s,unsigned fault,unsigned count){
 assert(sw_valid(s));rpe_response[0]=s->va;rpe_response[1]=s->pa;rpe_response[2]=s->capacity;rpe_response[3]=2;rpe_response[4]=0;
 rpe_fault=fault;rpe_count=count;rpe_last=rpe_native_calls=rpe_owned_calls=rpe_reads=rpe_ep_queries=rpe_ep_matches=rpe_ep_contract_verified=rpe_ep_context_observed=rpe_ep_fd_verified=rpe_ep_open_count=0;rpe_stop_requested=fault==8;rpe_trace_error=NULL;rpe_ep_error="NONE";
 char p[1400];snprintf(p,sizeof p,"%s/ep-replay.log",dir);rpe_log_file=fopen(p,"w");assert(rpe_log_file);int ok=rpe_ep_preflight(dir);assert(!fclose(rpe_log_file));
 if(ok)assert(rpe_ep_queries==4&&rpe_ep_matches==4&&rpe_native_calls==5&&rpe_ep_contract_verified);return ok;
}
#endif
'''
h=T/'core/tests/ep_preflight_fixture.h';h.write_text(header,encoding='utf8',newline='\n')
main=r'''
#include <stdlib.h>
#include "ep_preflight_fixture.h"
int main(int argc,char**argv){assert(argc==5);struct sw_lease s;assert(sw_file_read("/lease.bin",&s,sizeof s)&&sw_valid(&s));s.va=(uint32_t)strtoul(argv[1],NULL,0);sw_seal(&s);unsigned fault=(unsigned)atoi(argv[2]);int ok=rpe_run("/mnt/mmc",&s,fault,(unsigned)atoi(argv[4]));printf("ok=%d va=0x%08x pa=0x%08x queries=%u matches=%u native_calls=%u reason=%s\n",ok,s.va,s.pa,rpe_ep_queries,rpe_ep_matches,rpe_native_calls,rpe_ep_error);assert(ok==atoi(argv[3]));return 0;}
'''
def build(name,text):
 p=T/'core/tests'/('r11k3a_'+name+'.c');p.write_text(text,encoding='utf8',newline='\n');out=runs/(name+'.arm');r=subprocess.run([sys.executable,'-B',str(T/'core/tools/build_front.py'),'--zig',str(R/'work/tooling/python/ziglang/zig.exe'),'--cross-runtime',str(R/'work/tooling/arm-linux-musleabi-cross'),'--source',str(p),'--output',str(out)],env=env,capture_output=True,text=True,encoding='utf8');assert r.returncode==0,r.stderr[-5000:];return out
bins={}
for tag,p in [('previous',B/'hardware/src/alloc_core.h'),('fixed',T/'hardware/src/alloc_core.h')]:bins[tag]=build(tag,'#define NXL_TEST_EP_ALLOC_HEADER "'+p.as_posix()+'"\n'+main)
raw=(W/'camera-evidence/NXDEMO51/JOB_1794942114_524_73495e8b/front_1/front/shared-workspace.bin').read_bytes();rows=[]
for tag,binary in bins.items():
 cases=[('page-'+str(i),0x92920000+i*4096,0,int(tag=='fixed'or i==0),1)for i in range(4)]
 if tag=='fixed':cases += [('fault-'+str(i),0x92921000,i,0,1)for i in range(1,9)]+[('count5',0x92921000,0,1,5)]
 for name,va,fault,want,count in cases:
  d=runs/(tag+'-'+name);(d/'mnt/mmc').mkdir(parents=True);(d/'lease.bin').write_bytes(raw);m=PostFileModel(binary,[hex(va),str(fault),str(want),str(count)],d);a=m.run();assert a['exit']==0,(tag,name,a);rows.append(dict(variant=tag,case=name,expected=want,passed=True,stdout=a['stdout']));del m;gc.collect()
contract=r'''
#include <assert.h>
#include <stdio.h>
#include NXL_TEST_EP_ALLOC_HEADER
int main(void){unsigned n=0;struct ep_candidate q[2];uint32_t address;
 for(unsigned i=0;i<4;i++){uint32_t va=0x92920000+i*4096;assert(ep_make_candidates(va,0xa39ec000,q));n++;assert(q[0].y==va&&q[1].y==va+TEST_OUT);n++;assert(alloc_sample_range(va,0,128,&address)&&address==va);n++;assert(alloc_sample_range(va,ALLOC_BYTES-128,128,&address)&&address==va+ALLOC_BYTES-128);n++;assert(!alloc_sample_range(va,1,128,&address));n++;assert(!alloc_sample_range(va,ALLOC_BYTES-96,128,&address));n++;assert(!alloc_sample_range(va,0,127,&address));n++;assert(!ep_make_candidates(va,0xa39ed000,q));n++;}
 uint32_t bad[]={0,0x92921004,0xf8000000};for(unsigned i=0;i<3;i++){assert(!ep_make_candidates(bad[i],0xa39ec000,q));assert(!alloc_sample_range(bad[i],0,128,&address));n+=2;}printf("VA4K/PA16K contracts checks=%u\n",n);return 0;}
'''
for tag,p in [('core',T/'core/src/alloc_core.h'),('hardware',T/'hardware/src/alloc_core.h')]:
 binary=build('contract_'+tag,'#define NXL_TEST_EP_ALLOC_HEADER "'+p.as_posix()+'"\n'+contract);d=runs/('contract-'+tag);d.mkdir();m=PostFileModel(binary,[],d);a=m.run();assert a['exit']==0,a;rows.append(dict(variant=tag,case='contracts',passed=True,stdout=a['stdout']));del m;gc.collect()
result=dict(passed=True,case_count=len(rows),cases=rows,production_EP_preflight_function_sha256=hashlib.sha256(body.encode()).hexdigest(),production_preflight_unchanged=True,actual_captured_lease_sha256=hashlib.sha256(raw).hexdigest(),actual_VA='0x92921000',actual_PA='0xa39ec000',original_failure_reproduced=True,fixed_full_preflight_four_native_translation_queries_completed=True,native_UDD_calls_ownership_process_and_filesystem_endpoints_modeled=True,physical_camera_tested=False)
(W/'ep-preflight-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print('PASS actual EP preflight and both header contracts',len(rows),'ARM scenarios',flush=True)
