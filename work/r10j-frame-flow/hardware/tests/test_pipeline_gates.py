#!/usr/bin/env python3
"""Actual combined preflight gate, real idle kernel snapshot, modeled ownership."""
import argparse,json,subprocess,tempfile
from pathlib import Path
from test_gate_equivalence import PRE
ROOT=Path(__file__).resolve().parents[1]
BODY=r'''
int main(int argc,char **argv){
 assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f&&fread(report_kernel,1,sizeof report_kernel,f)==sizeof report_kernel);fclose(f);
 c.log=fopen("gate-main.log","w");c.gate_log=fopen("gate-details.log","w");assert(c.log&&c.gate_log);c.top=top;c.lut=jpeg;c.rtos=rtos;owner_captured=1;
 unsigned tests=0;
 for(unsigned kind=0;kind<2;kind++)for(unsigned locked=0;locked<(kind?4u:3u);locked++)for(unsigned fault=0;fault<(kind?15u:3u);fault++){
  c.lut_mode=kind;c.gate_rejections=0;memcpy(kernel_before,report_kernel,sizeof kernel_before);memcpy(raw_state,kernel_before,sizeof raw_state);memcpy(mutex_words,kernel_before+EP_KERNEL_STATE_BYTES,12);
  mutex_words[0]=locked?0:1;uint32_t core=kind&&locked==3?0x20:0,acc=locked==2?(kind?0x80:0x24):0;memcpy(raw_state+0x64,&core,4);memcpy(raw_state+0x74,&acc,4);
  if(fault==1)raw_state[0x64]^=1;if(fault==2)raw_state[0x74]^=1;
  if(fault>=3&&fault<12){unsigned queue=(fault-3)/3,part=(fault-3)%3,va=(uint32_t[]){0xc075551c,0xc0755528,0xc0755604}[queue],off=va-EP_KERNEL_STATE_VA+part*4;uint32_t bad=part?0:1;memcpy(raw_state+off,&bad,4);memcpy(kernel_before+off,&bad,4);}
  if(fault==12){uint32_t bad=1;memcpy(raw_state+0x68,&bad,4);memcpy(kernel_before+0x68,&bad,4);}
  if(fault==13)mutex_words[1]^=4;if(fault==14)owner_captured=0;
  int ok=state(&c,locked);assert(ok==(fault==0));owner_captured=1;tests++;
 }
 fclose(c.log);fclose(c.gate_log);printf("PASS %u combined JPEG/LUT callback ownership models; no hardware\n",tests);return 0;
}
'''
def main():
 p=argparse.ArgumentParser();p.add_argument('kernel_snapshot',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
 with tempfile.TemporaryDirectory() as td:
  s=Path(td)/'model.c';out=Path(td)/'model';s.write_text(PRE+'\n#include "jpeg_gate_diag.inc"\n'+BODY)
  subprocess.run(['gcc','-O1','-g','-fsanitize=undefined','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-misleading-indentation','-I',str(ROOT/'src'),str(s),'-o',str(out)],check=True)
  r=subprocess.run([str(out),str(a.kernel_snapshot.resolve())],cwd=td,capture_output=True,text=True,check=True);print(r.stdout,end='')
 if a.output:a.output.write_text(json.dumps({'passed':69,'scope':__doc__,'hardware_verified':False,'stdout':r.stdout},indent=2)+'\n')
if __name__=='__main__':main()
