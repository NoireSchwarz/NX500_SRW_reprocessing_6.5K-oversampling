#!/usr/bin/env python3
"""Independently check the published pointer facts against a stock P7 input.
No image is edited. No firmware data are bundled or emitted.
"""
from pathlib import Path
import argparse
import hashlib
import json
import struct

ap=argparse.ArgumentParser();ap.add_argument('p7',type=Path);args=ap.parse_args()
root=Path(__file__).resolve().parents[1];facts=json.loads((root/'DYNAMIC_MAPPING.json').read_text())
data=args.p7.read_bytes()
assert hashlib.md5(data).hexdigest()==facts['stock_p7_md5'],'wrong stock image'
assert hashlib.sha256(data).hexdigest()==facts['stock_p7_sha256'],'wrong stock image'
code=data[0x383800:0x383a24]
assert hashlib.sha256(code).hexdigest()==facts['code_sha256']
hash32=2166136261
for b in code:hash32=((hash32^b)*16777619)&0xffffffff
assert '%08x'%hash32==facts['code_fnv32'][2:]
targets=[]
for pos in range(0,len(code),4):
    word=struct.unpack_from('<I',code,pos)[0]
    if word&0x0f7f0000==0x051f0000: # ARM immediate LDR from PC, U bit either way
        disp=word&0xfff
        addr=0x80383800+pos+8+(disp if word&0x800000 else -disp)
        assert 0x80383a24<=addr<0x80383a84,'unexpected literal reference'
        targets.append(addr)
assert len(targets)==24 and set(targets)==set(range(0x80383a24,0x80383a84,4))
original=struct.unpack_from('<24I',data,0x383a24)
assert ['0x%08x'%x for x in original]==facts['factory_slots']
print('PASS: stock identity, selector code, all 24 ARM PC-relative literal references and factory values')
