#!/usr/bin/env python3
"""Recreate private build inputs from a pinned, separately obtained OEM ZIP.
Never install firmware or connect to a camera. Original binaries stay outside
the patch. Requires lzokay and dissect.extfs, used only on the build computer.
"""
import argparse,hashlib,json,struct,zipfile,zlib
from pathlib import Path
import lzokay
from dissect.extfs import ExtFS
BIN_SHA='f4c238e811970cf858b5d734119477ea191d35cd29d9f84b584157ecfc4ee997'
LIBS=[('/usr/lib/libudd5.so','libudd5.so','8c56bea30a591fbfb8e9e86c237504f5e28f464f74c18cffc8bbaa7f717b0ea1'),('/lib/libc-2.13.so','libc.so.6','198a7b52e68df955de2573557644720e76580aae489e3785bb9afd80ad6f2e9c'),('/usr/lib/libtint-util.so','libtint-util.so','370529ce2486fa9c65417857be652946e7a7541c2f31bb8819054cded9e85a22'),('/usr/lib/libmm-type.so','libmm-type.so','496257a466cd19d1db92146f5a6a3d634e776538a71604cc0c7a04978ba86ff5')]
def unpack_lzop(b,out):
 pos=9
 if b[:9]!=b'\x89LZO\0\r\n\x1a\n':raise ValueError('LZO container')
 def read(fmt):
  nonlocal pos
  n=struct.calcsize(fmt);v=struct.unpack_from(fmt,b,pos)[0];pos+=n;return v
 version=read('>H');read('>H');read('>H');read('>B');read('>B');flags=read('>I')
 if version<0x940:raise ValueError('LZO version')
 if flags&0x800:read('>I')
 read('>I');read('>I');read('>I');length=read('>B');pos+=length;end=pos;checksum=read('>I')
 if checksum!=(zlib.crc32(b[9:end]) if flags&0x1000 else zlib.adler32(b[9:end])):raise ValueError('LZO header checksum')
 total=0
 with out.open('wb') as f:
  while True:
   n=read('>I')
   if not n:break
   c=read('>I')
   if not 0<c<=n<=4*1024*1024:raise ValueError('LZO block size')
   da=read('>I') if flags&1 else None;dc=read('>I') if flags&0x100 else None
   ca=read('>I') if c<n and flags&2 else None;cc=read('>I') if c<n and flags&0x200 else None
   block=b[pos:pos+c];pos+=c
   if ca is not None and zlib.adler32(block)!=ca:raise ValueError('LZO compressed Adler')
   if cc is not None and zlib.crc32(block)!=cc:raise ValueError('LZO compressed CRC')
   data=lzokay.decompress(block,n) if c<n else block
   if len(data)!=n:raise ValueError('LZO output length')
   if da is not None and zlib.adler32(data)!=da:raise ValueError('LZO data Adler')
   if dc is not None and zlib.crc32(data)!=dc:raise ValueError('LZO data CRC')
   total+=n
   if total>1024*1024*1024:raise ValueError('LZO total bound')
   f.write(data)
 if pos!=len(b):raise ValueError('LZO trailing data')
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('firmware_zip',type=Path);p.add_argument('output',type=Path);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(a.firmware_zip) as z:b=z.read('NX500.bin')
 if hashlib.sha256(b).hexdigest()!=BIN_SHA:raise ValueError('Pinned NX5001.12 image mismatch')
 def part(i):
  size,_,start,_=struct.unpack_from('<4I',b,0x40+i*24)
  if start+size>len(b):raise ValueError('Partition bound')
  return b[start:start+size]
 root=a.output/'platform.img';unpack_lzop(part(6),root);manifest={}
 with root.open('rb') as f:
  fs=ExtFS(f)
  for src,name,expected in LIBS:
   data=fs.get(src).open().read();h=hashlib.sha256(data).hexdigest()
   if h!=expected:raise ValueError('Original DSO mismatch: '+src)
   (a.output/name).write_bytes(data);manifest[name]=h
 rtos=part(5);kernel_container=part(2);start=kernel_container.find(b'\x1f\x8b\x08');d=zlib.decompressobj(31);kernel=d.decompress(kernel_container[start:])
 for name,data,h in [('P7.bin',rtos,'f8e417cb5b55768851ba7e2b8017d3a98dc0b0c8444cc0a5061265cc08dbbf7f'),('kernel.Image',kernel,'25bdca5cde4a5c10710d02dbaa904c62cd77b0b4562b8bf4d73c675f50bdb587')]:
  if hashlib.sha256(data).hexdigest()!=h:raise ValueError('Original image mismatch: '+name)
  (a.output/name).write_bytes(data);manifest[name]=h
 (a.output/'original-inputs.json').write_text(json.dumps(manifest,indent=2)+'\n');print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
