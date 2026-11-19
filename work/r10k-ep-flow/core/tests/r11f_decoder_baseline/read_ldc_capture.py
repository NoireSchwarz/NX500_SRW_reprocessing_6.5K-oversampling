#!/usr/bin/env python3
"""Validate raw passive records and export retained packets; PC only.
No coefficient fitting/interpolation, MakerNote inference or camera writes.
"""
from pathlib import Path
import argparse,hashlib,json,struct,zlib
def frames(path):
 with path.open('rb')as f:
  while True:
   h=f.read(40)
   if not h:return
   if len(h)!=40:raise ValueError('partial header')
   v=struct.unpack('<10I',h)
   if v[0]!=0x3152584e or v[7]>65536 or zlib.crc32(h[:36])!=v[9]:raise ValueError('header identity/CRC')
   b=f.read(v[7])
   if len(b)!=v[7]or zlib.crc32(b)!=v[8]:raise ValueError('payload extent/CRC')
   yield v,b
def extract(report):
 schema=json.loads((report/'schema.json').read_text('utf8'));graph_schema=json.loads((report/'ldc-control-schema.json').read_text('utf8'));packets=[];graphs=[];counts={}
 for v,b in frames(report/'contexts.bin'):
  kind=v[1];counts[str(kind)]=counts.get(str(kind),0)+1;head=dict(sequence=v[2],begin_ns=v[3]|v[4]<<32,end_ns=v[5]|v[6]<<32,atomic=False)
  if kind==10:
   if len(b)!=schema['lpr_bytes']:raise ValueError('persistent record extent')
   status,mask=struct.unpack_from('<2I',b,4);source=b[52:324];storage=b[324:1452];banks=[]
   for at in[552,840]:
    bank=storage[at:at+288];banks.append(dict(header=list(struct.unpack('<8I',bank[:32])),table_hex=bank[32:].hex(),table_equals_current_source=bank[32:]==source[4:260]))
   packets.append(dict(**head,status=status,available_mask=mask,source_packet_hex=source.hex(),retained_banks=banks))
  elif kind==11:
   if len(b)!=graph_schema['bytes']:raise ValueError('control graph extent')
   words=struct.unpack('<'+str(len(b)//4)+'I',b);graphs.append(dict(**head,words=words))
 return dict(format='NX_LDC_CAPTURE_RAW_EXPORT1',report=report.name,CRC_verified=True,record_counts=counts,packets=packets,control_graphs=graphs,limits='Raw bounded metadata only. Matching bytes or stable guards are not a hardware-write trace or proof of atomicity. No MakerNote-to-table mapping or interpolation is assumed.')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('report',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();r=extract(a.report);a.output.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8',newline='\n');print('CRC verified; raw packets',len(r['packets']),'control graphs',len(r['control_graphs']))
if __name__=='__main__':main()
