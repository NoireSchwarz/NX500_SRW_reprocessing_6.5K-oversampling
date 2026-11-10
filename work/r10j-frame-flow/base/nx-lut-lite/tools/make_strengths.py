#!/usr/bin/env python3
"""Offline RGB-domain strength variants for exact, known native LUT files.

The BT.601 transforms are affine. Blend each node's unclipped RGB output
with the RGB input at that native node, then encode CrCbY and reconstruct
padding. This commutes with trilinear interpolation up to final byte rounding.
It is not byte scaling toward zero or mixing padding as colour data.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
from native_codec import GRID, SLOT, TABLE, sample, rgb_to_ycc, ycc_to_rgb, slotwrap

def validate(raw):
    if len(raw)!=SLOT:raise ValueError('native LUT must be 19712 bytes')
    if any(raw[i] for i in range(3,TABLE,4)):raise ValueError('nonzero node padding')
    if raw[TABLE:TABLE+12]!=raw[TABLE-4:TABLE]*3 or any(raw[TABLE+12:]):
        raise ValueError('invalid native tail')

def fnv32(raw):
    h=2166136261
    for b in raw:h=((h^b)*16777619)&0xffffffff
    return h

def variant(raw,percent):
    validate(raw)
    if percent not in (0,25,50,75,100):raise ValueError('unsupported strength')
    if percent==100:return raw
    alpha=percent/100.;out=bytearray();offset=0
    for y in GRID:
        for cb in GRID:
            for cr in GRID:
                identity=ycc_to_rgb((cr,cb,y),clip=False)
                filtered=ycc_to_rgb(raw[offset:offset+3],clip=False)
                rgb=[(1-alpha)*a+alpha*b for a,b in zip(identity,filtered)]
                out.extend((*[int(v+.5) for v in rgb_to_ycc(rgb)],0));offset+=4
    return slotwrap(out)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--looks',type=Path,action='append',required=True)
    ap.add_argument('--out',type=Path,required=True)
    args=ap.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    report={};seen=set()
    for folder in args.looks:
        for path in sorted(folder.glob('*.bin')):
            name=path.stem
            if not re.fullmatch(r'[A-Za-z0-9_-]{1,80}',name):raise ValueError('invalid LUT filename')
            if name in seen:raise ValueError('duplicate LUT filename')
            seen.add(name);raw=path.read_bytes();validate(raw)
            target=args.out/name;target.mkdir(exist_ok=True)
            (target/'source.bin').write_bytes(raw)
            record={'source_sha256':hashlib.sha256(raw).hexdigest(),'variants':{}}
            for strength in (25,50,75):
                data=variant(raw,strength);validate(data)
                (target/f'{strength}.bin').write_bytes(data)
                (target/f'{strength}.fnv').write_text(f'{fnv32(data):08x}\n')
                record['variants'][str(strength)]={'sha256':hashlib.sha256(data).hexdigest(),'fnv32':f'{fnv32(data):08x}'}
            report[name]=record
    (args.out/'manifest.json').write_text(json.dumps({'blend':'encoded RGB, before output clipping','strengths':[25,50,75,100],'100_percent':'original source unchanged','looks':report},indent=2)+'\n')
    print('Generated',len(report),'LUTs x 3 variants; 100% uses originals')
if __name__=='__main__':main()
