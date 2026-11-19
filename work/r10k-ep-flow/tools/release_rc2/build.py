from context import *
import importlib.util,re,shutil,math
sys.path.insert(0,str(PY));from PIL import Image,ImageDraw,ImageFont
from fontTools.ttLib import TTFont
U=T/'base/nx-lut-lite/src';D=T/'base/nx-lut-lite/data'
original=json.loads((D/'ui-glyphs.json').read_text('utf8'));glyphs=dict(original);needed=set()
for p in U.iterdir():
 if p.suffix in ['.c','.h','.inc']and p.name!='ui_font.h':needed.update(c for c in p.read_text('utf8')if ord(c)>=128)
fontpath=W.parent/'dependencies/original-tooling/NotoSansCJKsc-Medium.otf';cmap=TTFont(fontpath).getBestCmap();font={n:ImageFont.truetype(str(fontpath),n)for n in [23,24]}
for cp in sorted(ord(c)for c in needed if str(ord(c))not in glyphs):
 assert cp in cmap;ch=chr(cp);cjk=cp>=0x3000;f=font[24 if cjk else 23];left=f.getbbox(ch,anchor='ls')[0];im=Image.new('L',(26,28));ImageDraw.Draw(im).text((1 if cjk else max(0,-left),24 if cjk else 22),ch,font=f,anchor='ls',fill=255)
 glyphs[str(cp)]=dict(advance=26 if cjk else min(26,math.ceil(f.getlength(ch))+max(0,-left)),rows=[sum(1<<x for x in range(26)if im.getpixel((x,y))>=96)for y in range(28)])
assert all(glyphs[k]==v for k,v in original.items());put(D/'ui-glyphs.json',glyphs)
out=['#ifndef NXL_UI_FONT_H','#define NXL_UI_FONT_H','struct nxl_ui_glyph{unsigned cp,advance;unsigned row[28];};','static const struct nxl_ui_glyph nxl_ui_font[]={']
for cp,g in sorted(glyphs.items(),key=lambda x:int(x[0])):out.append('{'+cp+','+str(g['advance'])+',{'+','.join(hex(r)for r in g['rows'])+'}},')
out+=['};','static const struct nxl_ui_glyph*nxl_ui_glyph(unsigned cp){unsigned lo=0,hi=sizeof nxl_ui_font/sizeof*nxl_ui_font;while(lo<hi){unsigned mid=(lo+hi)/2;if(nxl_ui_font[mid].cp<cp)lo=mid+1;else hi=mid;}if(lo<sizeof nxl_ui_font/sizeof*nxl_ui_font&&nxl_ui_font[lo].cp==cp)return nxl_ui_font+lo;return nxl_ui_glyph(63);}','#endif'];(U/'ui_font.h').write_text('\n'.join(out)+'\n','utf8',newline='\n')
run('menu',[T/'base/nx-lut-lite/tools/build_zig.py','--zig',ZIG,'--menu-only','--elftools-path',PY])
shutil.copyfile(T/'base/nx-lut-lite/bin/nxl-menu',M/'bin/nxl-menu');shutil.copyfile(D/'ui-glyphs.json',M/'data/ui-glyphs.json');shutil.copyfile(T/'base/nx-lut-lite/auto-init.sh',M/'auto-init.sh')
shutil.copyfile(W.parent/'formal-0.4.0-rc1-work/runtime_identity.py',W/'runtime_identity.py')
owner=S/'nx-lut-demo/ownership';shutil.copyfile(B/'nx-lut-demo/module.manifest',owner/'rc1.manifest')
p=owner/'index.sha256';s=p.read_text('utf8');s=''.join(x+'\n'for x in s.splitlines()if not x.endswith(' rc1.manifest'));p.write_text(sha(owner/'rc1.manifest')+' rc1.manifest\n'+s,'utf8',newline='\n')
for n in ['install.sh','nx-lut-demo/deploy.sh']:
 p=S/n;s=p.read_text('utf8');assert 'release-0.4.0-rc1'in s or 'release-0.4.0-rc2'in s;p.write_text(s.replace('release-0.4.0-rc1','release-0.4.0-rc2'),'utf8',newline='\n')
spec=importlib.util.spec_from_file_location('rc2ri',W/'runtime_identity.py');ri=importlib.util.module_from_spec(spec);spec.loader.exec_module(ri);ri.bind(S)
manifest=S/'nx-lut-demo/module.manifest';manifest.write_text(''.join(sha(p)+' '+p.relative_to(M).as_posix()+'\n'for p in sorted(M.rglob('*'))if p.is_file()),'utf8',newline='\n')
p=S/'nx-lut-demo/deploy.sh';s=p.read_text('utf8')
for k,path in [('MANIFEST_SHA',manifest),('OWNERSHIP_INDEX_SHA',owner/'index.sha256')]:s,n=re.subn('^'+k+'=[0-9a-f]{64}$',k+'='+sha(path),s,flags=re.M);assert n==1
p.write_text(s,'utf8',newline='\n');audit=ri.audit(S);assert audit['passed'];put(W/'identity-validation.json',audit)
put(W/'build-validation.json',dict(passed=True,glyphs=len(glyphs),original_bitmaps_preserved=True,menu_sha256=sha(M/'bin/nxl-menu'),module_manifest_sha256=sha(manifest),hardware_tested=False))
print('PASS RC2 build and all runtime identities',flush=True)
