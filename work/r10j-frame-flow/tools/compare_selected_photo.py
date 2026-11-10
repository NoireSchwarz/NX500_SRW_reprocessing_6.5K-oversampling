"""Compare R8j control records for one selected SRW, using captured evidence.
Usage: python compare_selected_photo.py H:/ H:/NXDEMO51/JOB_... output-dir
Read-only card access. All generated files go into the explicit output folder.
Timing association is reported as an inference, never a hardware DMA proof.
"""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,struct,zlib
RECORD_BYTES=11920
STAGES=['BEFORE_CONFIG','AFTER_CONFIG','BEFORE_START','AFTER_START','AFTER_COMPLETE','AFTER_RESTORE']
def fnv(b):
 h=2166136261
 for x in b:h=((h^x)*16777619)&0xffffffff
 return h
def decode(b):
 assert len(b)==RECORD_BYTES
 v=list(struct.unpack('<2980I',b));assert v[0]==0x31435049 and v[1]==1
 assert v[-2]==fnv(b[:-8]) and v[-1]==(v[0]^v[2]^v[3]^v[-2])
 assert v[4]<=31 and v[5]<=1
 return dict(nonce=v[2],stage=v[3],mask=v[4],stable=bool(v[5]),pte=v[6:8],roots_before=v[8:12],roots_after=v[12:16],wb=v[16:34],banks=[v[34:1506],v[1506:2978]],sha256=hashlib.sha256(b).hexdigest())
def frames(path):
 with path.open('rb')as f:
  at=0
  while h:=f.read(40):
   assert len(h)==40,('partial frame header',at)
   v=struct.unpack('<10I',h);assert v[0]==0x3152584e and v[7]<=2*1024*1024 and zlib.crc32(h[:36])==v[9],at
   p=f.read(v[7]);assert len(p)==v[7] and zlib.crc32(p)==v[8],at
   yield dict(kind=v[1],sequence=v[2],begin_ns=v[3]|v[4]<<32,end_ns=v[5]|v[6]<<32),p
   at+=40+len(p)
def media_path(card,path):
 p=PurePosixPath(path);prefix='/mnt/mmc/'
 assert path.startswith(prefix)and'..'not in p.parts
 return card.joinpath(*PurePosixPath(path[len(prefix):]).parts)
def sha(path):
 h=hashlib.sha256()
 with path.open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
def compare(card,job,out):
 card=Path(card);job=Path(job);out=Path(out);out.mkdir(parents=True,exist_ok=True)
 sources=list(job.glob('front_*/front/selected-source.json'));assert len(sources)==1,'select one completed experiment job'
 front=sources[0].parent;source=json.loads(sources[0].read_text('utf8'));link=json.loads((front/'native-recorder-link.json').read_text('utf8'))
 raw=(front/'isp-config-stages.bin').read_bytes();assert len(raw)==6*RECORD_BYTES
 module=[decode(raw[i*RECORD_BYTES:(i+1)*RECORD_BYTES])for i in range(6)]
 frame=json.loads((front/'front-frame-result.json').read_text('utf8'))
 assert all(r['stage']==i+1 and r['nonce']==frame['nonce']for i,r in enumerate(module))
 assert source['source_sha256']==link['source_sha256']
 original=media_path(card,source['source_path']);jpeg=media_path(card,source['native_jpeg_path'])
 native_digest=sha(jpeg);recorded=source.get('native_jpeg_sha256','')
 identity=dict(source_hash_matches=sha(original)==source['source_sha256'],native_jpeg_hash_matches=native_digest==recorded if recorded else None,native_jpeg_size_matches=jpeg.stat().st_size==source['native_jpeg_bytes'],native_jpeg_full_hash_recorded_at_capture=bool(recorded),native_jpeg_sha256_at_PC=native_digest)
 assert identity['source_hash_matches'] and identity['native_jpeg_size_matches'] and (not recorded or identity['native_jpeg_hash_matches']),'source or recorded original JPEG identity changed after experiment'
 # R9i uses the reference only for metadata pairing. A new PC digest cannot
 # prove that its image bytes were unchanged since the camera experiment.
 result=dict(schema=1,release='R8m',job=job.name,source=source,identity=identity,module_stages=module,black=frame,
  native_pairing_verified=False,pairing_status='UNMATCHED',pairing_is_timeline_inference=True,shadow_is_dma_proof=False,ldc_applied=False,ldc_pixel_correctness_verified=False)
 settings_path=front/'pipeline-isp-settings.json'
 if settings_path.is_file():
  settings=json.loads(settings_path.read_text('utf8'));assert settings['source_sha256']==source['source_sha256'];result['runtime_native_settings']=settings;result['ldc_applied']=settings['ldc_jobs']==1 and settings['stage_completed']==3 and not settings['failed_stage'];result['jpeg_quant_verified']=settings['jpeg_quant_verified']==1
 def save():
  (out/'isp-compare.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
  status=result['pairing_status'];diff=result.get('native_vs_module',[])
  text='R8m 同源照片ISP对照\n\n源片：'+source['source_path']+'\n原生JPEG：'+source['native_jpeg_path']+'\n源片与原生JPEG文件指纹：已复核\n原生配置关联：'+status+'\n黑位已提交：'+str(frame.get('black_level_submitted',False))+'；源片黑位：'+str(source['black_rggb'])+'\n'
  text+='模块6个边界：'+str([dict(stage=STAGES[i],mask=r['mask'],stable=r['stable'])for i,r in enumerate(module)])+'\n'
  if diff:
   for d in diff:text+=d['stage']+'：WB不同字数'+str(len(d['wb_changes']))+'，DDR两组不同字数'+str([len(x)for x in d['bank_changes']])+'\n'
  text+='\n原生快照按新文件身份和活动时序关联，仍属于时序推断。不能把保留的空闲配置当作拍摄配置，也不能把DDR影子一致当作参数DMA已被硬件采用。静态A028黑位已接入；LDC是否执行以runtime_native_settings的原厂完成通知和恢复证据为准。动态黑位差分及按源片重载CC仍未启用。未知配置字保留原值，不猜测其Gamma/曝光含义。\n'
  (out/'对照结果.txt').write_text(text,encoding='utf8')
 if not link.get('recorder_report'):save();return result
 recorder=media_path(card,link['recorder_report']);events=[]
 with (recorder/'context.jsonl').open(encoding='utf8')as f:
  for line in f:
   if len(line)>1048576:raise ValueError('oversized observer event')
   try:e=json.loads(line)
   except json.JSONDecodeError:continue # last live-writer line may be incomplete
   if e.get('event')=='photo_inventory':events.append(e)
 selection=link['selection_mono_ns']
 matching=[e for e in events if e.get('phase')=='poll' and e['path']==source['source_path'] and e['bytes']==link['source_bytes'] and e.get('inode')==link['source_inode'] and e.get('device')==link['source_device'] and e['mtime']==link['source_mtime'] and e.get('mtime_ns')==link['source_mtime_ns'] and e['mono_ns']<selection]
 if not matching:result['pairing_status']='PHOTO_NOT_OBSERVED_AFTER_RECORDER_START';save();return result
 event=min(matching,key=lambda e:e['mono_ns']);end=event['mono_ns'];begin=max(0,end-8_000_000_000)
 # Multiple newly-created SRWs near the chosen file leave the pairing open.
 neighbors={e['path']for e in events if e.get('phase')=='poll' and e['path'].lower().endswith('.srw') and begin<=e['mono_ns']<=min(selection,end+1_000_000_000)}
 result['photo_event']=event;result['nearby_new_srw']=sorted(neighbors)
 if neighbors!={source['source_path']}:result['pairing_status']='MULTIPLE_PHOTOS_IN_CAPTURE_WINDOW';save();return result
 observed=[];ldc=[]
 for h,p in frames(recorder/'contexts.bin'):
  if not(begin<=h['begin_ns']<=end and h['end_ns']<selection):continue
  if h['kind']==12:
   r=decode(p)
   if r['stable']and r['mask']==31 and 3<=(r['roots_before'][1]&255)<=6:observed.append({**h,**r})
  elif h['kind']in[9,10,11]:
   name=f'native-kind{h["kind"]}-{h["sequence"]}.bin';(out/name).write_bytes(p);ldc.append(dict(**h,file=name,sha256=hashlib.sha256(p).hexdigest()))
 result['ldc_native_records']=ldc
 if not observed:result['pairing_status']='NO_STABLE_NATIVE_STILL_ACTIVE_SNAPSHOT';save();return result
 result['native_candidates']=observed
 # Keep all observed variants. A single final-looking snapshot alone cannot
 # prove the exact frame/parameter-DMA adoption point.
 best=max(observed,key=lambda e:e['end_ns']);result['native_comparison_candidate_sha256']=best['sha256'];result['pairing_status']='SINGLE_PHOTO_TIMELINE_CANDIDATE'
 result['native_vs_module']=[]
 for i,r in enumerate(module):
  if r['mask']!=31 or not r['stable']:continue
  result['native_vs_module'].append(dict(stage=STAGES[i],wb_changes=[dict(word=j,native=a,module=b)for j,(a,b)in enumerate(zip(best['wb'],r['wb']))if a!=b],bank_changes=[[dict(offset=j*4,native=a,module=b)for j,(a,b)in enumerate(zip(best['banks'][k],r['banks'][k]))if a!=b]for k in range(2)]))
 save();return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('card',type=Path);p.add_argument('job',type=Path);p.add_argument('output',type=Path);a=p.parse_args();r=compare(a.card,a.job,a.output);print(r['pairing_status'])
