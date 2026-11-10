#!/usr/bin/env python3
"""Execute actual p21b gate code with a modeled initial status observation.
Includes the unchanged18+81 regressions and the real p21a4->0 values.
No device, register write, IRQ or JPEG job is exercised.
"""
from pathlib import Path
import argparse,json,subprocess,tempfile
import test_gate_equivalence as old_tests
ROOT=Path(__file__).resolve().parents[1]
HOOK=r'''
static int settle_override;static unsigned settle_value,settle_reads;
static uint32_t modeled_settle_read(struct native_context *v){settle_reads++;if(settle_override)jpeg[0]=settle_value;return v->lut[0];}
#define JPEG_GATE_SETTLE_READ(v) modeled_settle_read(v)
'''
EXTRA=r'''
 struct settle_case {uint32_t before,after,dma;int bank;unsigned off,val;const char *reason;int ok;};
 static const struct settle_case settle_cases[]={SETTLE_CASES};
 for(unsigned n=0;n<sizeof settle_cases/sizeof *settle_cases;n++){
  const struct settle_case *t=settle_cases+n;reset_model();settle_override=1;settle_reads=0;settle_value=t->after;
  jpeg[0]=t->before;jpeg[0x800/4]=t->dma;
  if(t->bank>=0){mutation_bank=t->bank;mutation_offset=t->off;mutation_value=t->val;}
  int ok=idle(&c);assert(ok==t->ok);
  if(ok){assert(sleeps==16&&settle_reads==1&&c.gate_rejections==0);}
  else assert(c.gate_rejections==1&&c.gate_last_reason&&!strcmp(c.gate_last_reason,t->reason));
  if(t->before&3||t->dma&3)assert(settle_reads==0&&sleeps==0);
  else assert(settle_reads==1);
 }
 printf("PASS %u bounded completion-settling/status-bit models; full16samples after qualifying4->0; no hardware\n",(unsigned)(sizeof settle_cases/sizeof *settle_cases));
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('original_worker_source',type=Path);p.add_argument('real_kernel_snapshot',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 original=a.original_worker_source.read_text();old=original[original.index('static int queue_empty('):original.index('\nint main(')].replace('queue_empty','original_queue_empty').replace('static int state(','static int original_state(').replace('static int idle(','static int original_idle(')
 # Retain the99 actual callback regression cases from the original harness.
 cases=[(-1,0,0,0,'PASS'),(0,0x1020,0x80,0,'JPEG_CLOCK_GATED'),(0,0x1024,0x20000,0,'JPEG_CLOCK_GATED')]
 cases += [(2,0xaac+4*i,1,0,'RTOS_MASK_NONZERO') for i in range(5)]
 cases += [(2,0xc98,1,0,'RTOS_OWNER_NONZERO')]+[(0,x,1,0,'OTF_ACTIVE') for x in [0x1118,0x1218]]
 cases += [(0,0x1070+4*i,32,0,'TOP_MASK_DIFF') for i in range(5)]+[(0,0x10c8+4*i,32,0,'RAW_PENDING') for i in range(5)]
 cases += [(1,x,1,1,'JPEG_REGISTER_CHANGED') for x in [0,4,8,0x280,0x800,0x804,0x808,0x80c,0x810,0x814,0x818,0x820]]
 cases += [(0,(0x1700 if (i//2)%2 else 0x1300)+(i//8)*256+((i//4)%2)*128+(i%2)*4,16,1,'DMA_ADDRESS_CHANGED') for i in range(32)]
 cases += [(2,0xaac+4*i,1,1,'RTOS_MASK_CHANGED') for i in range(5)]
 cases += [(0,0x1070+4*i,1,1,'TOP_MASK_DIFF') for i in range(5)]+[(0,0x10c8+4*i,1,1,'RAW_PENDING') for i in range(5)]+[(-2,0,0,0,'SAMPLE_SLEEP_FAILED')]
 settling=[(4,0,0,-1,0,0,'PASS',1),(0,0,0,-1,0,0,'PASS',1),(4,4,0,-1,0,0,'PASS',1)]
 settling += [(v,0,0,-1,0,0,'JPEG_CONTROL_ACTIVE',0) for v in [1,2,3,5,6,7]]
 settling += [(4,0,v,-1,0,0,'JPEG_DMA_CONTROL_ACTIVE',0) for v in [1,2,3]]
 settling += [(v,x,0,-1,0,0,'JPEG_INITIAL_STATUS_CHANGED',0) for v,x in [(4,8),(8,0),(12,8),(0x104,0x100),(0xffffffff,0xfffffffb)] if not v&3]
 # Every new bit assertion is refused, including the completion bit itself.
 settling += [(0,1<<i,0,-1,0,0,'JPEG_INITIAL_STATUS_CHANGED',0) for i in range(32)]
 # Every initial single-bit disappearance is refused except the proven bit2.
 settling += [(1<<i,0,0,-1,0,0,'PASS' if i==2 else 'JPEG_CONTROL_ACTIVE' if i<2 else 'JPEG_INITIAL_STATUS_CHANGED',int(i==2)) for i in range(32)]
 # The same4->0 transition is refused if it happens INSIDE the stable window.
 settling += [(4,4,0,1,0,4,'JPEG_REGISTER_CHANGED',0)]
 # Qualifying initial settling never bypasses later completion/control changes,
 # raw IRQ activity, DMA movement or RTOS ownership changes.
 settling += [(4,0,0,1,0,v,'JPEG_REGISTER_CHANGED',0) for v in [1,2,4,8]]
 settling += [(4,0,0,0,0x10c8+4*i,32,'RAW_PENDING',0) for i in range(5)]
 settling += [(4,0,0,0,0x1300,16,'DMA_ADDRESS_CHANGED',0),(4,0,0,2,0xaac,1,'RTOS_MASK_CHANGED',0)]
 arr=','.join('{%d,%du,%du,%du,"%s"}'%t for t in cases)
 extra=','.join('{%uu,%uu,%uu,%d,%uu,%uu,"%s",%d}'%t for t in settling)
 post=old_tests.POST.replace('CASES',arr).replace(' fclose(c.log);fclose(c.gate_log);',EXTRA.replace('SETTLE_CASES',extra)+'\n fclose(c.log);fclose(c.gate_log);')
 with tempfile.TemporaryDirectory() as td:
  src=Path(td)/'model.c';exe=Path(td)/'model';src.write_text(old_tests.PRE+old+HOOK+'\n#include "jpeg_gate_diag.inc"\n'+post)
  subprocess.run(['gcc','-O1','-g','-fsanitize=undefined','-fno-omit-frame-pointer','-Wall','-Wextra','-Werror','-Wno-unused-function','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
  r=subprocess.run([str(exe),str(a.real_kernel_snapshot.resolve())],cwd=td,capture_output=True,text=True)
  if r.returncode:raise RuntimeError(r.stderr)
  print(r.stdout,end='')
 if a.output:a.output.write_text(json.dumps({'unchanged_regressions':99,'completion_settling_models':len(settling),'passed':99+len(settling),'observed_transition':[4,0],'full_stability_samples_after_initial_observation':16,'hardware_verified':False,'stdout':r.stdout},indent=2)+'\n')
if __name__=='__main__':main()
