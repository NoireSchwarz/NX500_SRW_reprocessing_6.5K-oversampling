#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""NX500 SRW MakerNote IQ reader; Python standard library, metadata reads only.

Supports the direct, outer-endian Samsung2 IFD found in NX500 native SRW.
Offsets are parsed from each file, never fixed to the example files' layout.
No RAW/JPEG payload is decoded and no input file is written.
"""
import argparse,json,struct
from pathlib import Path

TYPES={1:1,2:1,3:2,4:4,5:8,7:1,9:4,10:8}
NAMES={0xa014:'ISO',0xa020:'EncryptionKey',0xa021:'WB_RGGBLevelsUncorrected',
 0xa022:'WB_RGGBLevelsAuto',0xa023:'WB_RGGBLevelsIlluminator1',
 0xa024:'WB_RGGBLevelsIlluminator2',0xa028:'WB_RGGBLevelsBlack',
 0xa030:'ColorMatrix',0xa031:'ColorMatrixSRGB',0xa032:'ColorMatrixAdobeRGB'}

class Reader:
 def __init__(self,file):
  self.f=file;file.seek(0,2);self.size=file.tell();self.bytes_read=0
 def read(self,offset,n,limit=None):
  lo,hi=(0,self.size) if limit is None else limit
  if n<0 or offset<lo or offset>hi or n>hi-offset:raise ValueError('metadata extent outside containing region')
  self.f.seek(offset);b=self.f.read(n);self.bytes_read+=len(b)
  if len(b)!=n:raise ValueError('short metadata read')
  return b
 def ifd(self,offset,base=0,limit=None):
  count=struct.unpack(self.order+'H',self.read(offset,2,limit))[0]
  if count>512:raise ValueError('IFD entry budget exceeded')
  table=self.read(offset+2,count*12+4,limit);out={}
  for i in range(count):
   tag,typ,n,value=struct.unpack_from(self.order+'HHII',table,i*12)
   if tag in out:raise ValueError('duplicate IFD tag')
   out[tag]=(typ,n,offset+2+i*12+8 if typ in TYPES and TYPES[typ]*n<=4 else base+value)
  return out
 def value(self,entry,limit=None,max_bytes=256):
  typ,n,offset=entry
  if typ not in TYPES or n>max_bytes//TYPES[typ]:raise ValueError('unexpected tag type/count')
  raw=self.read(offset,n*TYPES[typ],limit)
  if typ==2:return raw.split(b'\0',1)[0].decode('ascii')
  if typ in [3,4,9]:return list(struct.unpack(self.order+str(n)+{3:'H',4:'I',9:'i'}[typ],raw))
  raise ValueError('unsupported value representation')

def s32(n):return ((n+(1<<31))%(1<<32))-(1<<31)
def normalize(v):
 if len(v)!=4 or any(x<=0 for x in v):raise ValueError('invalid WB levels')
 return [x/v[1] for x in v]

def read_info(path):
 path=Path(path)
 with path.open('rb') as file:
  r=Reader(file);head=r.read(0,8)
  if head[:2] not in [b'MM',b'II']:raise ValueError('not classic TIFF')
  r.order='>' if head[:2]==b'MM' else '<'
  magic,first=struct.unpack(r.order+'HI',head[2:])
  if magic!=42:raise ValueError('not classic TIFF')
  root=r.ifd(first);make=r.value(root[0x010f]);model=r.value(root[0x0110])
  if 'SAMSUNG' not in make.upper() or model.strip()!='NX500':raise ValueError('only native NX500 Samsung2 layout supported')
  exifptr=r.value(root[0x8769]);exif=r.ifd(exifptr[0]);typ,size,base=exif[0x927c]
  if typ!=7 or not 6<=size<=r.size-base:raise ValueError('invalid MakerNote extent')
  limit=(base,base+size);tags=r.ifd(base,base,limit)
  if tags[0xa020][:2]!=(4,11):raise ValueError('invalid Samsung key tag')
  key=r.value(tags[0xa020],limit);decoded={};tag_offsets={}
  for tag,name in NAMES.items():
   if tag not in tags:continue
   entry=tags[tag];raw=r.value(entry,limit);typ,n,off=entry
   expected=(4,1) if tag==0xa014 else (4,11) if tag==0xa020 else (9,9) if tag in [0xa030,0xa031,0xa032] else (9,4) if tag==0xa028 else (4,4)
   if (typ,n)!=expected:raise ValueError('invalid type/count for '+name)
   if tag in [0xa021,0xa022,0xa023,0xa024,0xa028]:
    salt={0xa021:0,0xa022:4,0xa023:8,0xa024:1,0xa028:0}[tag]
    value=[(x-key[(salt+i)%11])%(1<<32) for i,x in enumerate(raw)]
    if typ==9:value=[s32(x) for x in value]
   elif tag in [0xa030,0xa031,0xa032]:value=[s32(x+key[i]) for i,x in enumerate(raw)]
   else:value=raw
   decoded[name]=value;tag_offsets[name]=dict(tag=hex(tag),file_offset=off,bytes=n*TYPES[typ],encoded_values=raw)
  uncorrected=decoded['WB_RGGBLevelsUncorrected'];black=decoded['WB_RGGBLevelsBlack'];corrected=[a-b for a,b in zip(uncorrected,black)]
  raw_n=normalize(uncorrected);corrected_n=normalize(corrected)
  matrices={name:dict(integer_3x3=[decoded[name][i:i+3] for i in range(0,9,3)],q8_interpretation_3x3=[[v/256 for v in decoded[name][i:i+3]] for i in range(0,9,3)]) for name in ['ColorMatrix','ColorMatrixSRGB','ColorMatrixAdobeRGB'] if name in decoded}
  flag=r.value(exif[0xa403])[0] if 0xa403 in exif else None
  return dict(file=path.name,file_bytes=r.size,make=make,model=model,tiff_byte_order='MM' if r.order=='>' else 'II',makernote_offset=base,makernote_bytes=size,metadata_bytes_read=r.bytes_read,raw_unpacked=False,input_written=False,
   iso=decoded.get('ISO',[None])[0],exif_white_balance_flag=flag,
   decoded_tags=decoded,tag_locations=tag_offsets,wb=dict(file_order='RGGB',uncorrected_rggb=uncorrected,uncorrected_normalized_rggb=raw_n,uncorrected_normalized_rgbg=[raw_n[0],raw_n[1],raw_n[3],raw_n[2]],black_rggb=black,exiftool_composite_rggb=corrected,exiftool_composite_normalized_rggb=corrected_n,q16_approx_from_uncorrected_rggb=[int(v*65536+.5) for v in raw_n],native_interface_form_not_yet_verified=True),matrices=matrices)

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('srw',type=Path);args=p.parse_args()
 try:print(json.dumps(read_info(args.srw),ensure_ascii=False,indent=2))
 except (OSError,ValueError,KeyError,struct.error) as e:p.exit(1,'SRW_METADATA_ERROR: '+str(e)+'\n')
