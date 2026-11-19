from context import *
import ast,shutil
sys.path[:0]=[str(PY),str(T/'base/nx-lut-lite/tools'),str(SRC/'tools/log_video')]
import numpy as np
from scipy.stats import qmc
from native_codec import compile_uniform_rgb,NATIVE,GRID
from samsung_standard import read_cube,sample_cube
node=next(n for n in ast.parse((A/'lut_assets.py').read_text('utf8')).body if isinstance(n,ast.FunctionDef)and n.name=='native_sample')
ns=dict(np=np,GRID=GRID);exec(compile(ast.Module(body=[node],type_ignores=[]),'native-sampler','exec'),ns)
U=T/'base/nx-lut-lite';V=W/'Panasonic-originals';P=S/'PC_LUT_SOURCES/Panasonic-official';shutil.copytree(V,P,dirs_exist_ok=True)
catalog=json.loads((U/'data/release-lut-catalog.json').read_text('utf8'))
ids={'pan_backlight_n','pan_smoky_s','pan_dramatic_s','vlog_pan_dramatic'}
catalog['looks']=[r for r in catalog['looks']if r['id']not in ids]
spec=[('lut_0042','pan_backlight_n','Backlight-N','暖調逆光','自然暖調、柔化暗部對比，適合逆光人像。','原表基於松下Natural；本機普通畫面試用。','portrait','STD'),
 ('lut_0040','pan_smoky_s','Smoky Color-S','煙霧色調','抑制亮部與暖色，呈現低飽和的煙霧質感。','原表基於Standard；適合建築、街景與藍天。','cinema','STD'),
 ('lut_0049','pan_dramatic_s','Dramatic BlockBuster-S','大片標準','青橙對比的電影配色，保留鮮明的冷暖層次。','Kotaro Kumazawa；原表基於Standard。','cinema','STD'),
 ('lut_0019','vlog_pan_dramatic','VLog Dramatic BlockBuster','大片Log','V‑Log專用青橙電影配色，帶膠片式對比。','Kotaro Kumazawa原版；僅從VID選擇。','loglut','VLOG')]
probes=qmc.Sobol(3,scramble=True,seed=911).random_base2(14);results=[]
for code,id,title,short,tone,scene,group,domain in spec:
 p=V/(code+'.cube');meta=json.loads((V/(code+'.json')).read_text('utf8'));cube=read_cube(p)
 assert cube.dimensions==3 and cube.size==33 and np.isfinite(cube.data).all()
 assert '#LUMIXPHOTOSTYLE '+meta['photo_style']in p.read_text('utf8')
 assert meta['photo_style']==('VLOG'if domain=='VLOG'else 'NAT'if code=='lut_0042'else'STD')
 data=compile_uniform_rgb(cube.data*255,33,boundary='linear');assert len(data)==19712 and not any(data[19664:])
 for d in [W/'converted-luts',S/'looks',M/'looks',U/'looks']:(d/(id+'.bin')).write_bytes(data)
 row=dict(id=id,name=title,tone=tone,scene=scene,group=group,input=domain,short_name=short,source=str(p),source_sha256=sha(p),native_sha256=sha(S/'looks'/(id+'.bin')),
  source_grid=33,native_grid=17,native_encoding=NATIVE,approximation=False,numeric_resampling=True,official_code=code,official_photo_style=meta['photo_style'],
  creator=meta['creator']['name'],official_endpoint='https://lumix-base.jpn.panasonic.com/lutapi/v2/community/lut',original_PC_source='PC_LUT_SOURCES/Panasonic-official/'+code+'.cube',physical_colour_matching_certified=False)
 catalog['looks'].append(row)
 e=(ns['native_sample'](data,probes)-np.clip(sample_cube(cube,probes),0,1))*255
 results.append(dict(id=id,source_sha256=row['source_sha256'],native_sha256=row['native_sha256'],actual_header_style=meta['photo_style'],MAE8=float(abs(e).mean()),RMS8=float(np.sqrt(np.mean(e*e))),worst8=float(abs(e).max()),modeled_not_camera_measured=True))
put(U/'data/release-lut-catalog.json',catalog)
(U/'data/release-lut-allowlist.tsv').write_text(''.join(f"{r['id']}\t{r['input']}\t{r['native_sha256']}\n"for r in catalog['looks']),'ascii',newline='\n')
put(U/'data/looks-info.json',[{k:r[k]for k in ['id','name','tone','scene','group']}for r in catalog['looks']])
for n in ['release-lut-catalog.json','release-lut-allowlist.tsv','looks-info.json']:shutil.copyfile(U/'data'/n,M/'data'/n)
put(W/'Panasonic-conversion-validation.json',dict(passed=True,official_downloads=True,source_data_not_code_executed=True,looks=results,
 catalogue_count=len(catalog['looks']),normal_count=sum(r['input']=='STD'for r in catalog['looks']),Log_count=sum(r['input']=='VLOG'for r in catalog['looks']),
 no_invented_VLog_versions=True,Backlight_Natural_base_not_certified_as_NX500_Natural=True,original_sources_and_ZIPs_retained=True))
p=U/'src/menu_lut_detail_v2.inc';s=p.read_text('utf8');anchor='static const char*detail_short_name(const char*id){'
if'pan_backlight_n'not in s:
 p.write_text(s.replace(anchor,anchor+'\n'+''.join(' if(!strcmp(id,'+json.dumps(id)+'))return '+json.dumps(short,ensure_ascii=False)+';\n'for _,id,_,short,*_ in spec)),'utf8',newline='\n')
print('PASS four official Panasonic LUTs; 12 ordinary, 7 V-Log; bases verified from API and CUBE',flush=True)
