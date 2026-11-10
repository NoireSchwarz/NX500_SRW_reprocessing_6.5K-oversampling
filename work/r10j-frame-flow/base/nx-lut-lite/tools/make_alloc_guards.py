#!/usr/bin/env python3
"""Generate small exact-build code guards; original libraries are not shipped."""
import argparse,hashlib,json,struct
from pathlib import Path
from elftools.elf.elffile import ELFFile
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('shell',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
out=['/* Generated exact NX500 1.12 guards. */','#ifndef NXL_ALLOC_GUARDS_H','#define NXL_ALLOC_GUARDS_H','struct alloc_guard {unsigned offset,words[8];};']
manifest={}
for label,path,addresses in [('CAP',a.capture,[0x4ccb0,0x4cd04,0x4a05c,0x677b0,0x84124]),('SHELL',a.shell,[0x71b0,0x7340,0x7158,0xc6c8])]:
 b=path.read_bytes();h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 manifest[label]={'bytes':len(b),'fnv32':hex(h),'sha256':hashlib.sha256(b).hexdigest(),'guards':addresses}
 out.extend([f'#define ALLOC_{label}_BYTES {len(b)}u',f'#define ALLOC_{label}_FNV 0x{h:08x}u',f'static const struct alloc_guard alloc_{label.lower()}_guards[]={{'])
 with path.open('rb') as f:
  e=ELFFile(f)
  for address in addresses:
   seg=next(s for s in e.iter_segments() if s['p_type']=='PT_LOAD' and s['p_vaddr']<=address and address+32<=s['p_vaddr']+s['p_filesz'])
   raw=seg.data()[address-seg['p_vaddr']:address-seg['p_vaddr']+32]
   out.append('{0x%xu,{%s}},'%(address,','.join('0x%08xu'%x for x in struct.unpack('<8I',raw))))
 out.append('};')
out.append('#endif');a.output.write_text('\n'.join(out)+'\n');a.output.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
