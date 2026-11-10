#!/usr/bin/env python3
"""Rasterize a reproducible, runtime-free Noto Sans CJK SC bitmap subset.
The external original OTF is a developer input; only glyph bitmaps ship.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
from PIL import Image, ImageDraw, ImageFont
from fontTools.ttLib import TTFont

root=Path(__file__).resolve().parents[1]
ap=argparse.ArgumentParser()
ap.add_argument('--font',required=True,type=Path)
ap.add_argument('--extra',default='',help='additional characters to include')
args=ap.parse_args()
glyphs=json.loads((root/'data/ui-glyphs.json').read_text())
chars={chr(int(k)) for k in glyphs}|set(args.extra)
face=TTFont(args.font);cmap=face.getBestCmap()
assert all(ord(ch) in cmap for ch in chars),'source font lacks a requested character'
fonts={n:ImageFont.truetype(str(args.font),n) for n in (23,24)}
output={}
for ch in sorted(chars):
    cjk=ord(ch)>=0x3000
    font=fonts[24 if cjk else 23]
    left=font.getbbox(ch,anchor='ls')[0]
    x=1 if cjk else max(0,-left)
    im=Image.new('L',(26,28));ImageDraw.Draw(im).text((x,24 if cjk else 22),ch,font=font,anchor='ls',fill=255)
    rows=[sum(1<<x for x in range(26) if im.getpixel((x,y))>=96) for y in range(28)]
    advance=26 if cjk else min(26,math.ceil(font.getlength(ch))+max(0,-left))
    output[str(ord(ch))]={'advance':advance,'rows':rows}
(root/'data/ui-glyphs.json').write_text(json.dumps(output,ensure_ascii=False,separators=(',',':'))+'\n')
record={'family':'Noto Sans CJK SC Medium','license':'SIL Open Font License 1.1',
        'source_url':'https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Medium.otf',
        'source_sha256':hashlib.sha256(args.font.read_bytes()).hexdigest(),
        'glyph_count':len(output),'cjk_pixels':24,'latin_pixels':23,'canvas':[26,28],
        'baseline_cjk':24,'baseline_latin':22,'threshold':96}
(root/'UI_FONT.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
print('Generated',len(output),'sans glyphs')
