#!/usr/bin/env python3
"""Developer helper: add UI-only ISAS Song24 glyphs from an existing PCF.
The generated bitmap JSON is shipped; the camera needs no font package.
"""
import argparse
import gzip
import json
from pathlib import Path
from PIL import Image,PcfFontFile

root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser();ap.add_argument('--pcf',type=Path,required=True);args=ap.parse_args()
path=root/'data/ui-glyphs.json';glyphs=json.loads(path.read_text())
text=''.join(p.read_text() for p in (root/'src/menu.c',root/'tools/make_ui_assets.py',root/'data/looks-info.json'))
needed=sorted({c for c in text if ord(c)>=0x3000 and str(ord(c)) not in glyphs})
with (gzip.open(args.pcf,'rb') if args.pcf.suffix=='.gz' else args.pcf.open('rb')) as file:
    font=PcfFontFile.PcfFontFile(file)
    metrics=font._load_metrics();bitmaps=font._load_bitmaps(metrics)
    fp,fmt,i16,i32=font._getformat(PcfFontFile.PCF_BDF_ENCODINGS)
    c0,c1,r0,r1,default=[i16(fp.read(2)) for _ in range(5)]
    indices=[i16(fp.read(2)) for _ in range((c1-c0+1)*(r1-r0+1))]
    for ch in needed:
        code=ch.encode('gb2312')
        if len(code)!=2:raise ValueError('Not a two-byte GB2312 character: '+ch)
        a,b=code[0]-128,code[1]-128
        if not(r0<=a<=r1 and c0<=b<=c1):raise ValueError('Font misses '+ch)
        index=indices[(a-r0)*(c1-c0+1)+(b-c0)]
        if index==65535:raise ValueError('Font misses '+ch)
        canvas=Image.new('1',(26,28));canvas.paste(bitmaps[index],(0,1))
        rows=[sum(1<<x for x in range(26) if canvas.getpixel((x,y))) for y in range(28)]
        glyphs[str(ord(ch))]={'advance':26,'rows':rows}
path.write_text(json.dumps(glyphs,ensure_ascii=False,separators=(',',':'))+'\n')
print('Added',len(needed),'UI glyphs:', ''.join(needed))
