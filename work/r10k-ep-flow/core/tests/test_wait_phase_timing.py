from pathlib import Path
import subprocess,hashlib,json
S=Path(__file__).resolve().parents[1];W=S.parents[1];B=S/'build/fnv-r6';B.mkdir(exist_ok=True);V=S/'results';V.mkdir(exist_ok=True)
s=(S/'src/front_control.inc').read_text();start=s.index('static int wait_done(void){');i=start+s[start:].index('{')+1;depth=1
while depth:
 if s[i]=='{':depth+=1
 elif s[i]=='}':depth-=1
 i+=1
fn=s[start:i];assert 'until=wait_started+60000000000ULL' in fn and 'pause_ms(5)' in fn and 'fflush' not in fn and fn.count('result_copy(')==3
c=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define P39_DONE 7
#define P48_PHASE_DONE 4
struct p39_result{uint32_t w[64];};
static struct p39_result observed_result;static int stop_requested,reply_ok,cancel_after;static uint64_t clock_ns;static FILE *log_file;static unsigned reads,seqn;static uint32_t seq[16],extra[16][4];static int unstable;
static uint64_t ns(void){return clock_ns;}
static unsigned ms(void){return (unsigned)(clock_ns/1000000);}
static void pause_ms(unsigned n){assert(n==5);clock_ns+=(uint64_t)n*1000000;if(cancel_after>0&&--cancel_after==0)stop_requested=1;}
static int result_copy(struct p39_result *r){memset(r,0,sizeof *r);unsigned call=reads++,i=call/2;if(i>=seqn)i=seqn-1;r->w[3]=seq[i];r->w[12]=extra[i][0];r->w[14]=extra[i][1];r->w[22]=extra[i][2];r->w[23]=extra[i][3];if(unstable&&(call&1)){r->w[3]=100;r->w[14]=0xffffffffu;}return 1;}
static int p48_read_reply(unsigned p){assert(p==P48_PHASE_DONE);return reply_ok;}
'''+fn+r'''
static unsigned logs(void){fflush(log_file);rewind(log_file);char b[512];unsigned n=0;while(fgets(b,sizeof b,log_file)){assert(strstr(b,"RTOS_PHASE_OBSERVED phase=")&&strstr(b,"frame_irq=")&&strstr(b,"dma_irq=")&&strstr(b,"source_fnv=")&&strstr(b,"target_fnv=")&&strstr(b,"sampled=1")&&strstr(b,"elapsed_ms=")&&strstr(b,"wait_ms="));n++;}return n;}
static void setup(const uint32_t *p,unsigned n){memcpy(seq,p,n*4);memset(extra,0,sizeof extra);unstable=0;seqn=n;reads=clock_ns=0;stop_requested=0;reply_ok=1;cancel_after=0;log_file=tmpfile();assert(log_file);}
int main(void){
 const uint32_t ok[]={8,3,3,4,6,6,7};setup(ok,7);assert(wait_done()==1&&observed_result.w[3]==7&&clock_ns==30000000&&logs()==5);fclose(log_file);
 const uint32_t fail[]={100};setup(fail,1);assert(wait_done()==0&&clock_ns==0&&logs()==1);fclose(log_file);
 const uint32_t busy[]={6};setup(busy,1);assert(wait_done()==0&&clock_ns==60000000000ULL&&reads==24001&&logs()==1);fclose(log_file);
 const uint32_t bad_done[]={7};setup(bad_done,1);reply_ok=0;cancel_after=3;assert(wait_done()==0&&clock_ns==15000000&&logs()==1);fclose(log_file);
 setup(busy,1);stop_requested=1;assert(wait_done()==0&&clock_ns==0&&logs()==0);fclose(log_file);
 const uint32_t tuples[]={6,6,6,6,7};setup(tuples,5);
 for(unsigned i=0;i<5;i++){extra[i][0]=i>=1;extra[i][1]=i>=2;extra[i][2]=0x12345678u;extra[i][3]=i>=3?0x91abcdefu:0;}
 assert(wait_done()==1&&clock_ns==20000000&&logs()==5);fclose(log_file);
 setup(busy,1);unstable=1;cancel_after=3;assert(wait_done()==0&&clock_ns==15000000&&logs()==0);fclose(log_file);
 puts("PASS7 real wait_done models: phase/IRQ/FNV tuple changes, unstable snapshots never logged, unchanged60s deadline/5ms polling, DONE token/error/cancellation preserved");return 0;}
'''
c=c.replace('tmpfile()', 'fopen('+json.dumps(str(B/'phase-test.log'))+',"w+")')
p=B/'test_wait_phase.c';p.write_text(c);exe=B/'test_wait_phase.host';subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True);q=subprocess.run([str(exe)],capture_output=True,text=True,check=True);print(q.stdout,end='')
result=dict(format='NX500_R6_LINUX_PHASE_TIMING_TESTS1',passed=True,hardware_tested=False,cases=7,stdout=q.stdout,source_sha256=hashlib.sha256((S/'src/front_control.inc').read_bytes()).hexdigest(),extracted_wait_done_sha256=hashlib.sha256(fn.encode()).hexdigest(),test_source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),scope='Only buffered Linux sampled observations after existing double-copy equality of phase,frameIRQ,DMAIRQ,sourceFNV,targetFNV; no additional result reads, RTOS clock, writes or native API added.')
(V/'wait-phase-r6-tests.json').write_text(json.dumps(result,indent=2)+'\n')
