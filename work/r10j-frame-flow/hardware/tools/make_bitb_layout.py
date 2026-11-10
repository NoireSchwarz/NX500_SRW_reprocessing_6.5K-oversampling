#!/usr/bin/env python3
"""Derive pointer-free wire offsets using the host C ABI; ARM --abi confirms size."""
import argparse,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();root=Path(__file__).resolve().parents[1]
source=(root/'src/bitb_job.h').read_text();body=re.search(r'struct bitb_result\s*\{(.*?)\};',source,re.S)[1];fields=[]
for line in body.split(';'):
 line=line.strip()
 if not line:continue
 kind,line=line.split(None,1)
 assert kind in ['uint32_t','int32_t']
 for item in line.split(','):
  m=re.fullmatch(r'(\w+)(?:\[(\w+)\])?',item.strip());assert m,item;count=30 if m[2]=='BITB_CONFIG_COUNT' else int(m[2]) if m[2] else 1;fields.append((m[1],count,kind=='int32_t'))
build=root/'build-bitb';build.mkdir(exist_ok=True);program=build/'layout.c';lines=['#include <stdio.h>','#include "../src/bitb_job.h"','int main(void){','printf("{\\\"request_bytes\\\":%u,\\\"result_bytes\\\":%u,\\\"fields\\\":[",(unsigned)sizeof(struct bitb_request),(unsigned)sizeof(struct bitb_result));']
for i,(name,n,signed) in enumerate(fields):
 obj={'name':name,'offset':'OFFSET','count':n,'signed':signed};fmt=json.dumps(obj,separators=(',',':')).replace('"OFFSET"','%u');fmt=(',' if i else '')+fmt
 lines.append('printf('+json.dumps(fmt)+',(unsigned)offsetof(struct bitb_result,'+name+'));')
lines+=['puts("]}");return 0;}'];program.write_text('\n'.join(lines)+'\n');binary=build/'layout'
subprocess.run(['gcc','-O2',str(program),'-o',str(binary)],check=True);layout=json.loads(subprocess.check_output([str(binary)],text=True));a.output.write_text(json.dumps(layout,indent=2)+'\n');assert layout['fields'][-1]['offset']+4==layout['result_bytes'];print('Wire ABI',layout['request_bytes'],layout['result_bytes'],'bytes')
