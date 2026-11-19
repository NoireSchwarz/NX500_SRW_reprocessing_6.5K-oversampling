"""Read provided tables as data; never use archive names as output commands."""
from context import *
import shutil
archive=Path(r'F:\相册存档\NX500\looks.rar');dest=W/'provided-looks';dest.mkdir()
listing=subprocess.check_output(['tar','-tf',str(archive)],text=True,encoding='utf8').splitlines()
verbose=subprocess.check_output(['tar','-tvf',str(archive)],text=True,encoding='utf8').splitlines()
assert all(x[0]in '-d'for x in verbose)
for name in listing:
 rel=Path(name);assert not rel.is_absolute()and not rel.drive and '..'not in rel.parts and ':'not in name and '\\'not in name
 assert dest.resolve()in(dest/rel).resolve().parents
subprocess.run(['tar','-xf',str(archive),'-C',str(dest)],check=True)
manifest=json.loads((dest/'looks/nxl-strengths/manifest.json').read_text('utf8'))['looks']
catalog=json.loads((M/'data/release-lut-catalog.json').read_text('utf8'));existing={r['id']:r for r in catalog['looks']}
inventory=[]
for p in sorted((dest/'looks').glob('*.bin')):
 id=p.stem;data=p.read_bytes();assert sha(p)==manifest[id]['source_sha256']
 assert len(data)==19712 and not any(data[19664:])and data[19652:19664]==data[19648:19652]*3
 assert all(data[i]==0 for i in range(3,19652,4))
 why='skin/5207/Gold200 excluded'if any(k in id for k in ['skin','5207','gold200','g200'])else'old approximate excluded'if '_apx'in id else'already installed'if id in existing else'import'
 inventory.append(dict(id=id,sha256=sha(p),action=why))
 if id in existing:assert sha(p)==existing[id]['native_sha256'],('shared native baseline differs',id)
spec=[
 ('film_cinestill','CineStill 電影風格','偏冷暗部配暖色亮部，帶電影膠片感。','適合夜景與街景；為色彩風格，非光暈或顆粒效果。','cinema'),
 ('fuji_400h','Fuji Pro 400H','柔和色彩與清淡綠調，明暗對比較柔。','適合日光人像、花卉與生活記錄。','portrait'),
 ('fuji_astia100f','Fuji Astia 100F','較柔和的正片色彩，保留細緻中間調。','適合人像、旅行與柔和日光。','portrait'),
 ('fuji_classicchrome','Fuji Classic Chrome','降低飽和度並加強冷暖分離，紀實風格。','適合街拍、建築與陰天。','retro'),
 ('fuji_eterna500','Fuji Eterna 500','低對比、較克制的電影色調。','適合影片與柔和光線；普通畫面輸入。','cinema'),
 ('fuji_velvia50','Fuji Velvia 50','強烈飽和與對比，綠藍色較鮮明。','適合自然風景；高飽和色可能較易裁切。','landscape'),
 ('kodak_2383','Kodak 2383','電影拷貝方向的色彩與較深暗部。','本檔沿用既有普通畫面轉換，不作Log輸入表。','cinema'),
 ('kodak_ektar100','Kodak Ektar 100','鮮明色彩與清晰明暗，偏暖膠片風格。','適合日光風景、建築與旅行。','landscape'),
 ('mono_acros','Acros 黑白','黑白映射，強調明暗分離與質感。','適合街拍、建築與紀實；不增加顆粒。','mono'),
 ('pentax_satobi','Pentax Satobi','低飽和的暖調與褪色感。','適合懷舊街景、人像與生活場景。','retro'),
 ('ricoh_retro','Ricoh Retro','偏暖、較低飽和的懷舊色彩。','適合旅行、街拍與日常記錄。','retro'),
]
assert {r['id']for r in inventory if r['action']=='import'}=={r[0]for r in spec}
originals=S/'PC_LUT_SOURCES/Imported-looks-RC3';originals.mkdir();shutil.copyfile(archive,originals/'looks.rar')
for id,title,tone,scene,group in spec:
 p=dest/'looks'/(id+'.bin')
 for folder in [S/'looks',M/'looks',T/'base/nx-lut-lite/looks']:
  folder.mkdir(exist_ok=True);shutil.copyfile(p,folder/p.name)
 catalog['looks'].append(dict(id=id,name=title,tone=tone,scene=scene,group=group,input='STD',source='provided looks.rar/looks/'+p.name,
  source_sha256=sha(p),native_sha256=sha(p),native_grid=17,native_encoding='CrCbY-BT601-full-v1',approximation=False,
  imported_preconverted_table=True,original_PC_source='PC_LUT_SOURCES/Imported-looks-RC3/looks.rar',physical_colour_matching_certified=False))
U=T/'base/nx-lut-lite';put(U/'data/release-lut-catalog.json',catalog)
(U/'data/release-lut-allowlist.tsv').write_text(''.join(r['id']+'\t'+r['input']+'\t'+r['native_sha256']+'\n'for r in catalog['looks']),'ascii',newline='\n')
profiles=[{k:r[k]for k in ['id','name','tone','scene','group']}for r in catalog['looks']];put(U/'data/looks-info.json',profiles)
for name in ['release-lut-catalog.json','release-lut-allowlist.tsv','looks-info.json']:shutil.copyfile(U/'data'/name,M/'data'/name)
p=U/'src/look_info.h';s=p.read_text('utf8');begin=s.index('static const struct nxl_look_info nxl_look_infos[]={')
q=lambda x:json.dumps(x,ensure_ascii=False)
s=s[:begin]+'static const struct nxl_look_info nxl_look_infos[]={\n'+''.join('{'+','.join(q(r[k])for k in ['id','name','tone','scene','group'])+'},\n'for r in profiles)+'};\n#endif\n'
s=s.replace('NXL_LOOK_INFO_VERSION "0.4.0-RC1"','NXL_LOOK_INFO_VERSION "0.4.0-RC3"');p.write_text(s,'utf8',newline='\n')
assert len(catalog['looks'])==30 and sum(r['input']=='STD'for r in catalog['looks'])==23
put(W/'LUT-import-validation.json',dict(passed=True,archive_sha256=sha(archive),inventory=inventory,imported=len(spec),catalog_count=30,ordinary_count=23,Log_count=7,
 shared_three_native_baselines_byte_exact=True,native_table_layout_valid=True,preconverted_sources_unchanged=True,
 old_strength_presets_not_activated=True,M3_continuous_strength_preserved=True,excluded_approximate_by_user_confirmation=True,hardware_tested=False))
print('PASS imported 11 native tables; 30 total (23 standard, 7 Log)',flush=True)
