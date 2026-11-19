"""PC-only R11k same-SRW evidence audit. Never writes camera/card files."""
from pathlib import Path
import argparse,struct,json,hashlib,zlib
PROFILE_X=[0,95,209,296,393,516,696,953,1307,1804,2491,2949,3346,4578,5995,7991,10461,13464,16383]
PROFILE_LOG=[126,164,232,270,301,331,363,398,433,468,504,522,536,571,601,632,662,690,712]
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def load(p):return json.loads(Path(p).read_text('utf8'))
def sample(p,source,nonce,boundary):
 b=Path(p).read_bytes();assert len(b)==13136,'sample extent';q=struct.unpack_from('<16I',b);assert q[:3]==(0x3153504e,1,13136)and q[3]==1 and q[4]==boundary and q[5]==nonce,'sample identity/status';assert q[6:8]==(6480,4320),'sample geometry';assert b[64:128].decode('ascii')==source,'sample source';expected=[(((6480*(2*x+1)//10-8)&~1),4320*(2*y+1)//10-8)for y in range(5)for x in range(5)];assert list(struct.iter_unpack('<II',b[132:332]))==expected,'sample coordinates';assert struct.unpack_from('<I',b,len(b)-4)[0]==fnv(b[:-4]),'sample checksum';return b[332:-4]
def audit(front,card=None):
 front=Path(front);mp=front/'gamma-transfer-profile.json';meta=load(mp)if mp.is_file()else None
 if meta is not None:
  assert meta['format']=='NX_SRW_TRANSFER_R11K'and meta['release']=='R11k','runtime revision';assert meta['bank']==1 and meta['gamma_only']is True and not meta['direct_official_LUT_input_certified'],'scope'
 b=(front/'gamma-test-audit.bin').read_bytes();assert len(b)==2048,'Gamma audit extent';q=list(struct.unpack('<512I',b));mode=q[3];assert mode in range(5)and (meta is None or mode==meta['mode']),'mode';assert q[0]==0x314d474e and q[1]==(2 if mode>=3 else 1)and (meta is None or q[1]==meta['request_schema']),'request schema';assert q[2]!=0 and (meta is None or q[2]==meta['nonce']),'nonce';assert zlib.crc32(b[:252])&0xffffffff==q[63],'request CRC';assert q[5:7]==[1,1]and not any(q[8:16]+q[54:63]),'request reserved'
 c0=(front/'selected-c0.bin').read_bytes();assert len(c0)==2048 and fnv(c0)==q[7]and(meta is None or q[7]==meta['source_c0_fnv']),'source C0';sx=list(struct.unpack_from('<19I',c0,356));sy=list(struct.unpack_from('<19I',c0,432));wantx=PROFILE_X if mode>=3 else sx;wanty=[(x*1023+8191)//16383 for x in PROFILE_X]if mode==3 else PROFILE_LOG if mode==4 else sy;assert q[16:35]==wantx and q[35:54]==wanty,'profile nodes'
 for i,value in {64:1,71:0,72:2,76:1,77:1,78:1,80:0,81:1,82:1,83:1,84:0,85:0}.items():assert q[i]==value,'Gamma lifecycle word '+str(i)
 if mode:
  for i,value in {65:1,66:1,67:1,68:1,69:0,70:1}.items():assert q[i]==value,'Gamma apply/restore word '+str(i)
 else:assert q[65]==q[68]==0,'factory unexpected writer'
 cb=(front/'source-cc-audit.bin').read_bytes();wb=(front/'source-wb-audit.bin').read_bytes();assert len(cb)==4096 and len(wb)==1024,'WB/CC audit extent';cc=list(struct.unpack('<1024I',cb));w=list(struct.unpack('<256I',wb));assert cc[:4]==[0x43433438,1,q[2],1]and cc[63]==fnv(cb[:252])and q[4]==cc[63],'CC request binding';assert w[:4]==[0x42573134,3,q[2],1]and w[47]==q[7]and w[63]==fnv(wb[:252]),'WB request binding';assert [v-0x100000000 if v&0x80000000 else v for v in cc[9:27]]==list(struct.unpack_from('<18h',c0,524)),'source CC coefficients'
 for i,value in {66:1,67:0,68:1,69:0,70:1,71:0,72:0,73:0,75:1,76:1,77:1}.items():assert cc[i]==value,'CC lifecycle word '+str(i)
 src=load(front/'selected-source.json');assert w[8:12]==src['wb_q8']and w[16:20]==src['black_rggb'],'source WB/black';source=src['source_sha256'];assert len(source)==64 and all(c in'0123456789abcdef'for c in source),'source SHA';isp=load(front/'pipeline-isp-settings.json');assert isp['source_sha256']==source and isp['stage_completed']==3 and isp['failed_stage']==0,'completed ISP stages'
 job=next(p for p in front.parents if p.name.startswith('JOB_'));results=sorted((job/'jpeg').glob('attempt_*/result.json'));found=[load(p)for p in results if load(p).get('nonce')==q[2]];assert len(found)==1,'unique same-request result';r=found[0];assert r['exit']==0 and r['workspace_return_verified']is True and r['jpeg_saved']is True,'completed workspace retirement';assert r['source_srw_sha256']==source,'coordinator source';assert r['native_jobs']==4,'native job count'
 ic=(front/'isp-config-stages.bin').read_bytes();assert len(ic)==6*11920,'ISP snapshot extent';stages=[];target=q[236:344]
 def masked(v):return [n&(0x7fff7fff if i%54<11 else 0x07ff07ff if i%54<22 else 0xffff if i%54==53 else 0xffffffff)for i,n in enumerate(v)]
 for i in range(6):
  data=ic[i*11920:(i+1)*11920];head=struct.unpack_from('<8I',data);tail=struct.unpack_from('<2I',data,11912);assert head[:4]==(0x31435049,1,q[2],i+1)and head[4]<=31 and head[5]<=1 and tail[0]==fnv(data[:-8])and tail[1]==(0x31435049^q[2]^(i+1)^tail[0]),'ISP snapshot identity';banks=[]
  for bank in [0,1]:
   off=136+bank*5888;raw=struct.unpack_from('<108I',data,off+0x1228);h=list(struct.unpack_from('<108H',data,off+0x1228+216));banks.append({'bank':bank,'Gamma_hash':hashlib.sha256(struct.pack('<108I',*raw)).hexdigest(),'X19':[v&32767 for v in h[3:22]],'Y19':[(v&2047)-2048 if v&1024 else v&2047 for v in h[25:44]],'matches_requested_Gamma_defined_fields':masked(raw)==masked(target),'source_CC_target_equal':list(struct.unpack_from('<10I',data,off+0x1200))==cc[90:100]})
  stages.append({'stage':i+1,'mask':head[4],'stable':bool(head[5]),'banks':banks})
 jpeg=None;rawpath=r.get('jpeg_path','')
 if card is not None and rawpath.startswith('/mnt/mmc/'):
  rel=Path(rawpath[len('/mnt/mmc/'):]);assert not rel.is_absolute()and'..'not in rel.parts,'JPEG path';jpeg=Path(card)/rel
 if jpeg is None or not jpeg.is_file():
  options=[p.parent/'native.jpg'for p in results if p.parent.joinpath('native.jpg').is_file()];jpeg=options[0]if len(options)==1 else None
 image_verified=bool(jpeg and jpeg.is_file()and sha(jpeg)==r['jpeg_sha256'])
 if image_verified:
  from PIL import Image
  with Image.open(jpeg)as im:assert im.format=='JPEG'and im.size==(6480,4320),'JPEG format/geometry';im.load()
 pixels={};missing=[]
 for name,boundary in [('bayer-out',1),('ldc-out',2),('lut-out',3)]:
  p=front/(name+'-samples.bin')
  if p.is_file():pixels[name]=sample(p,source,q[2],boundary)
  else:missing.append(p.name)
 return {'front':str(front),'job':str(job),'mode':mode,'curve_metadata_source':'gamma-transfer-profile.json'if meta is not None else'gamma-test-audit.bin','request_schema':q[1],'nonce':q[2],'source_sha256':source,'C0_sha256':hashlib.sha256(c0).hexdigest(),'WB_q8':src['wb_q8'],'black_rggb':src['black_rggb'],'source_CC_bytes':c0[524:560].hex(),'LDC_table_sha256':isp['ldc_table_sha256'],'LUT_sha256':r['lut_sha256'],'source_native_jpeg_sha256':src['native_jpeg_sha256'],'jpeg_path':str(jpeg)if jpeg else None,'jpeg_sha256':r['jpeg_sha256'],'jpeg_hash_verified':image_verified,'restored_bank0_shadow_same_observed':q[79]==1,'missing_pixel_samples':missing,'pixel_samples':pixels,'Gamma_lifecycle_verified':True,'ISP_parameter_snapshots':stages,'stable_bank1_target_parameter_snapshots':sum(t['stable']and(t['mask']&16)!=0 and t['banks'][1]['matches_requested_Gamma_defined_fields']for t in stages),'hardware_pixel_correctness_certified':False}
def compare(records):
 assert len(records)>=3,'need at least SOURCE19/LINEAR/LOG outputs';assert {1,3,4}.issubset({r['mode']for r in records}),'missing transfer profile';keys=['source_sha256','C0_sha256','WB_q8','black_rggb','source_CC_bytes','LDC_table_sha256','LUT_sha256','source_native_jpeg_sha256']
 for key in keys:assert all(r[key]==records[0][key]for r in records),'changed control: '+key
 assert len({r['nonce']for r in records})==len(records),'reused request';complete=all(r['jpeg_hash_verified']and not r['missing_pixel_samples']for r in records);source=[r for r in records if r['mode']==1];repeat=None
 if len(source)>1:
  repeat={k:source[0]['pixel_samples'].get(k)==source[-1]['pixel_samples'].get(k)for k in ['bayer-out','ldc-out','lut-out']};complete=complete and all(repeat.values())
 else:complete=False
 clean=[{k:v for k,v in r.items()if k!='pixel_samples'}for r in records];delta={}
 if all('bayer-out'in r['pixel_samples']for r in records):
  import numpy as np
  p={m:next(r['pixel_samples']['bayer-out']for r in records if r['mode']==m)for m in [1,3,4]};arrays={m:np.frombuffer(v,dtype=np.uint8).reshape(25,512)for m,v in p.items()}
  for name,m in [('LINEAR',3),('LOG_TRANSFER',4)]:
   diff=arrays[m].astype(int)-arrays[1].astype(int);delta[name]={'Y_mean_delta':float(diff[:,:256].mean()),'Y_MAE':float(abs(diff[:,:256]).mean()),'Y_changed_pixels':int((diff[:,:256]!=0).sum())}
  # Keep raw matched neutral tiles for PC fitting; no unsupported color-matrix assumption.
  neutral=[]
  for tile in range(25):
   lin=arrays[3][tile];log=arrays[4][tile];chromas=np.r_[lin[256:],log[256:]];spread=float(np.std(lin[:256]))
   if max(abs(chromas.astype(float)-128))<=4 and spread<5 and 6<float(np.mean(lin[:256]))<240:neutral.append({'tile':tile,'linear_Y_mean8':float(lin[:256].mean()),'Log_Y_mean8':float(log[:256].mean()),'linear_Y_std8':spread})
 else:neutral=[]
 return {'format':'NX_SRW_TRANSFER_PC_RESULT1','control_identity_verified':True,'complete_evidence_set':complete,'records':clean,'same_RAW_SOURCE19_repeat_equal':repeat,'Bayer_output_changes_vs_SOURCE19':delta,'curve_effect_in_Bayer_samples_observed':bool(delta and all(v['Y_changed_pixels']>0 for v in delta.values())),'neutral_tiles_for_followup_fit':neutral,'absolute_exposure_scale_certified':False,'complete_Samsung_Log_color_compatibility_certified':False,'gamma_parameter_equality_not_a_hardware_consumption_proof':True,'SD_written':False}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('fronts',nargs='+',type=Path);p.add_argument('--card',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();result=compare([audit(f,a.card)for f in a.fronts]);assert not a.output.exists(),'refuse overwriting report';a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8');print(json.dumps({'complete_evidence_set':result['complete_evidence_set'],'report':str(a.output)},ensure_ascii=False));raise SystemExit(0 if result['complete_evidence_set']else 3)
